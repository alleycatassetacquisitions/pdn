//
// Created by Elli Furedy on 10/13/2024.
//
#pragma once

#include <atomic>
#include <cstdint>

#include <button_gpio.h>
#include <sdkconfig.h>
#include <iot_button.h>

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"

/**
 * One button, on iot_button's state machine but this driver's threading.
 *
 * iot_button raises its events from an esp_timer task, but every callback the
 * game registers expects the main loop. So the event handler below only records
 * what fired and exec() dispatches it, keeping game code off the timer task.
 */
class Esp32S31ButtonDriver : public ButtonDriverInterface {
public:
    /** Takes the GPIO; the button is active-low with the internal pull-up. */
    Esp32S31ButtonDriver(const std::string& name, int buttonPin)
        : ButtonDriverInterface(name), buttonPin(buttonPin) {}

    ~Esp32S31ButtonDriver() override {
        if (handle != nullptr) {
            iot_button_delete(handle);
            handle = nullptr;
        }
    }

    int initialize() override {
        button_config_t buttonConfig = {};
        buttonConfig.long_press_time = LONG_PRESS_MS;
        buttonConfig.short_press_time = CLICK_RESOLVE_MS;

        button_gpio_config_t gpioConfig = {};
        gpioConfig.gpio_num = buttonPin;
        gpioConfig.active_level = 0;      // active low
        gpioConfig.disable_pull = false;  // internal pullup

        if (iot_button_new_gpio_device(&buttonConfig, &gpioConfig, &handle) != ESP_OK) {
            LOG_E(name.c_str(), "iot_button_new_gpio_device failed on pin %d", buttonPin);
            return -1;
        }

        // Every interaction is registered with the component whether or not the
        // game has a handler for it, and LONG_PRESS in particular must stay even
        // though nothing registers one: onButtonEvent reads BUTTON_LONG_PRESS_START
        // to know a long press is running, and without it the release that ends a
        // long press would be dispatched as a click.
        for (int interaction = 0; interaction < INTERACTION_COUNT; interaction++) {
            button_event_t event = eventFor(static_cast<ButtonInteraction>(interaction));
            // BUTTON_MULTIPLE_CLICK is the one event the component refuses to
            // register without arguments, because the count is what it matches
            // on.
            button_event_args_t args = {};
            button_event_args_t* argsPtr = nullptr;
            if (event == BUTTON_MULTIPLE_CLICK) {
                args.multiple_clicks.clicks = MULTI_CLICK_COUNT;
                argsPtr = &args;
            }
            if (iot_button_register_cb(handle, event, argsPtr, onButtonEvent, this) != ESP_OK) {
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
        pending[static_cast<int>(interactionType)].store(0);
    }

    void setButtonPress(parameterizedCallbackFunction newFunction, void* parameter,
                        ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        Slot& slot = slots[static_cast<int>(interactionType)];
        slot.plain = nullptr;
        slot.parameterized = newFunction;
        slot.parameter = parameter;
        // The timer task records presses from initialize() on, including through
        // the boot splash; a press made before any handler existed belongs to no
        // one and must not reach the handler registered now.
        pending[static_cast<int>(interactionType)].store(0);
    }

    void removeButtonCallbacks() override {
        // Undispatched presses are cleared too: they belong to the state that is
        // going away, and a dropped press beats one delivered after it dismounts.
        for (int i = 0; i < INTERACTION_COUNT; i++) {
            slots[i] = Slot{};
        }
        for (auto& count : pending) {
            count.store(0);
        }
    }

    void exec() override {
        for (int i = 0; i < INTERACTION_COUNT; i++) {
            // Counted rather than flagged: the masher scores one penalty per tap,
            // so two taps inside one loop pass have to dispatch twice. Drained in
            // enum order, not arrival order, so a state that registers two
            // interactions on one button sees all PRESSes before all CLICKs.
            uint8_t fired = pending[i].exchange(0);
            for (; fired > 0; fired--) {
                const Slot& slot = slots[i];
                if (slot.parameterized != nullptr) {
                    slot.parameterized(slot.parameter);
                } else if (slot.plain != nullptr) {
                    slot.plain();
                }
            }
        }
    }

    /**
     * True only while the button is still held past the long-press threshold.
     *
     * Read from the current event rather than from a duration: the component does
     * not reset its tick counter when a long press is released, so a duration
     * alone keeps reporting the press that already ended.
     */
    bool isLongPressed() override {
        if (handle == nullptr) {
            return false;
        }
        const button_event_t current = iot_button_get_event(handle);
        return current == BUTTON_LONG_PRESS_START || current == BUTTON_LONG_PRESS_HOLD;
    }

    /** How long a long press still in progress has been held, 0 otherwise. */
    unsigned long longPressedMillis() override {
        return isLongPressed() ? iot_button_get_pressed_time(handle) : 0;
    }

private:
    struct Slot {
        callbackFunction plain = nullptr;
        parameterizedCallbackFunction parameterized = nullptr;
        void* parameter = nullptr;
    };

    static constexpr int INTERACTION_COUNT = static_cast<int>(ButtonInteraction::RELEASE) + 1;
    // The double/multi-click disambiguation window only. CLICK maps to PRESS_UP,
    // which does not wait on it, so no single click pays this latency; the value
    // keeps a double-click reachable at a human cadence.
    static constexpr uint16_t CLICK_RESOLVE_MS = 400;
    static_assert(CONFIG_BUTTON_DEBOUNCE_TICKS * CONFIG_BUTTON_PERIOD_TIME_MS == 50,
                  "debounce is ticks x period in sdkconfig.defaults; see the comment there");
    static constexpr uint16_t LONG_PRESS_MS = 800;
    static constexpr uint16_t MULTI_CLICK_COUNT = 3;

    static button_event_t eventFor(ButtonInteraction interaction) {
        switch (interaction) {
            case ButtonInteraction::PRESS:             return BUTTON_PRESS_DOWN;
            // A completed press, which the component reports the moment the
            // button comes up. BUTTON_SINGLE_CLICK is a different thing: it
            // cannot fire until short_press_time has ruled out a second press,
            // so using it would make every click wait out the double-click
            // window.
            case ButtonInteraction::CLICK: return BUTTON_PRESS_UP;
            case ButtonInteraction::DOUBLE_CLICK:      return BUTTON_DOUBLE_CLICK;
            case ButtonInteraction::MULTI_CLICK:       return BUTTON_MULTIPLE_CLICK;
            case ButtonInteraction::LONG_PRESS:        return BUTTON_LONG_PRESS_START;
            case ButtonInteraction::DURING_LONG_PRESS: return BUTTON_LONG_PRESS_HOLD;
            case ButtonInteraction::RELEASE:           return BUTTON_LONG_PRESS_UP;
        }
        return BUTTON_EVENT_MAX;
    }

    /** Runs on iot_button's timer task, so it only records what happened. */
    static void onButtonEvent(void* buttonHandle, void* userData) {
        auto* driver = static_cast<Esp32S31ButtonDriver*>(userData);
        button_event_t event = iot_button_get_event(static_cast<button_handle_t>(buttonHandle));

        // The component raises PRESS_UP on every release, including the release
        // that ends a long press, which must not also count as a click, so it is
        // swallowed here. Both writes and the read happen on the component's timer
        // task, in event order.
        if (event == BUTTON_LONG_PRESS_START) {
            driver->longPressSeen = true;
        } else if (event == BUTTON_PRESS_DOWN) {
            driver->longPressSeen = false;
        } else if (event == BUTTON_PRESS_UP && driver->longPressSeen) {
            return;
        }

        for (int i = 0; i < INTERACTION_COUNT; i++) {
            if (eventFor(static_cast<ButtonInteraction>(i)) == event) {
                // Saturating compare-exchange, not a load/store pair: exec() drains
                // this with exchange(0) from the main loop, so a plain store could
                // resurrect counts it had already dispatched. Saturating at all
                // because the counter is 8-bit and wrapping would report zero.
                uint8_t queued = driver->pending[i].load();
                while (queued < UINT8_MAX &&
                       !driver->pending[i].compare_exchange_weak(queued, queued + 1)) {
                }
            }
        }
    }

    int buttonPin;
    button_handle_t handle = nullptr;
    Slot slots[INTERACTION_COUNT];
    // Written on the timer task, drained on the main loop.
    std::atomic<uint8_t> pending[INTERACTION_COUNT] = {};
    std::atomic<bool> longPressSeen{false};
};
