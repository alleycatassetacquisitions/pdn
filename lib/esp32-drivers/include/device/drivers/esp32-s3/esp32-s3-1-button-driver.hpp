//
// Created by Elli Furedy on 10/13/2024.
//
#pragma once

#include <atomic>

#include <button_gpio.h>
#include <iot_button.h>

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"

/**
 * One button, on iot_button's state machine but this driver's threading.
 *
 * iot_button raises its events from an esp_timer task. OneButton, which this
 * replaced, ran its whole state machine inside tick() on the main loop, so
 * every callback the game registers has only ever run there. Rather than move
 * all of them to a timer task, the event handler below only marks what fired
 * and exec() dispatches on the main loop, where tick() used to.
 */
class Esp32S31ButtonDriver : public ButtonDriverInterface {
public:
    /** Takes the GPIO; the button is active-low with the internal pull-up, as before. */
    Esp32S31ButtonDriver(const std::string& name, int buttonPin)
        : ButtonDriverInterface(name), buttonPin(buttonPin) {}

    ~Esp32S31ButtonDriver() override {
        if (handle != nullptr) {
            iot_button_delete(handle);
            handle = nullptr;
        }
    }

    int initialize() override {
        // OneButton's own defaults, kept because the press timings are what
        // the interactions were designed around.
        button_config_t buttonConfig = {};
        buttonConfig.long_press_time = LONG_PRESS_MS;
        buttonConfig.short_press_time = CLICK_MS;

        button_gpio_config_t gpioConfig = {};
        gpioConfig.gpio_num = buttonPin;
        gpioConfig.active_level = 0;    // OneButton(pin, activeLow = true)
        gpioConfig.disable_pull = false;  // OneButton(pin, _, pullupActive = true)

        if (iot_button_new_gpio_device(&buttonConfig, &gpioConfig, &handle) != ESP_OK) {
            LOG_E(name.c_str(), "iot_button_new_gpio_device failed on pin %d", buttonPin);
            return -1;
        }

        for (int interaction = 0; interaction < INTERACTION_COUNT; interaction++) {
            button_event_t event = eventFor(static_cast<ButtonInteraction>(interaction));
            if (iot_button_register_cb(handle, event, nullptr, onButtonEvent, this) != ESP_OK) {
                LOG_E(name.c_str(), "iot_button_register_cb failed for event %d", event);
                return -1;
            }
        }
        return 0;
    }

    void setButtonPress(callbackFunction newFunction,
                        ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        Slot& slot = slots[static_cast<int>(interactionType)];
        slot.plain = newFunction;
        slot.parameterized = nullptr;
        slot.parameter = nullptr;
    }

    void setButtonPress(parameterizedCallbackFunction newFunction, void* parameter,
                        ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        Slot& slot = slots[static_cast<int>(interactionType)];
        slot.plain = nullptr;
        slot.parameterized = newFunction;
        slot.parameter = parameter;
    }

    void removeButtonCallbacks() override {
        // OneButton::reset() cleared only its own press-tracking state and left
        // the callback pointers attached, which is why callers detach
        // DURING_LONG_PRESS by hand. This clears the slots outright, so that
        // habit is now belt rather than the only thing holding.
        for (int i = 0; i < INTERACTION_COUNT; i++) {
            slots[i] = Slot{};
        }
        pending.store(0);
    }

    void exec() override {
        uint32_t fired = pending.exchange(0);
        for (int i = 0; i < INTERACTION_COUNT; i++) {
            if ((fired & (1U << i)) == 0) {
                continue;
            }
            const Slot& slot = slots[i];
            if (slot.parameterized != nullptr) {
                slot.parameterized(slot.parameter);
            } else if (slot.plain != nullptr) {
                slot.plain();
            }
        }
    }

    bool isLongPressed() override {
        return handle != nullptr && iot_button_get_ticks_time(handle) >= LONG_PRESS_MS;
    }

    unsigned long longPressedMillis() override {
        return handle == nullptr ? 0 : iot_button_get_ticks_time(handle);
    }

private:
    struct Slot {
        callbackFunction plain = nullptr;
        parameterizedCallbackFunction parameterized = nullptr;
        void* parameter = nullptr;
    };

    static constexpr int INTERACTION_COUNT = 7;
    static constexpr uint16_t CLICK_MS = 400;
    static constexpr uint16_t LONG_PRESS_MS = 800;

    static button_event_t eventFor(ButtonInteraction interaction) {
        switch (interaction) {
            case ButtonInteraction::PRESS:             return BUTTON_PRESS_DOWN;
            case ButtonInteraction::CLICK:             return BUTTON_SINGLE_CLICK;
            case ButtonInteraction::DOUBLE_CLICK:      return BUTTON_DOUBLE_CLICK;
            case ButtonInteraction::MULTI_CLICK:       return BUTTON_MULTIPLE_CLICK;
            case ButtonInteraction::LONG_PRESS:        return BUTTON_LONG_PRESS_START;
            case ButtonInteraction::DURING_LONG_PRESS: return BUTTON_LONG_PRESS_HOLD;
            // OneButton attached RELEASE to attachLongPressStop.
            case ButtonInteraction::RELEASE:           return BUTTON_LONG_PRESS_UP;
        }
        return BUTTON_EVENT_MAX;
    }

    /** Runs on iot_button's timer task, so it only records what happened. */
    static void onButtonEvent(void* buttonHandle, void* userData) {
        auto* driver = static_cast<Esp32S31ButtonDriver*>(userData);
        button_event_t event = iot_button_get_event(static_cast<button_handle_t>(buttonHandle));
        for (int i = 0; i < INTERACTION_COUNT; i++) {
            if (eventFor(static_cast<ButtonInteraction>(i)) == event) {
                driver->pending.fetch_or(1U << i);
            }
        }
    }

    int buttonPin;
    button_handle_t handle = nullptr;
    Slot slots[INTERACTION_COUNT];
    // Written on the timer task, drained on the main loop.
    std::atomic<uint32_t> pending{0};
};
