//
// Created by Elli Furedy on 10/13/2024.
//
#pragma once

#include <atomic>

#include <button_gpio.h>
#include <sdkconfig.h>
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
        button_config_t buttonConfig = {};
        buttonConfig.long_press_time = LONG_PRESS_MS;
        // Not OneButton's 400ms _click_ms, deliberately. That number was never a
        // wait there: OneButton resolved a click the moment the button came up
        // unless a double- or multi-click handler was attached (`_nClicks ==
        // _maxClicks` short-circuits the wait at OneButton.cpp:293), and nothing
        // in this project attaches one. iot_button has no such short-circuit --
        // it always waits short_press_time after release to rule out a second
        // press -- so carrying 400ms across made every click resolve 400ms late
        // and folded anything faster into a multi-click that nothing handles.
        // Entering a four-digit pairing code is unusable at that rate.
        buttonConfig.short_press_time = CLICK_RESOLVE_MS;

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
            // BUTTON_MULTIPLE_CLICK is the one event the component refuses to
            // register without arguments, because the count is what it matches
            // on. OneButton's attachMultiClick fired from the third click.
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
        // the callback pointers attached. This clears the slots outright.
        //
        // pending is cleared with them, on purpose: an event the timer task
        // recorded but exec() has not dispatched belongs to the state that is
        // going away, and dispatching it afterwards is the teardown race this
        // project has already been bitten by. A dropped press beats a press
        // delivered into a dismounted state.
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
            // enum order, which is the order the component raises them in.
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

    /** True only while the button is still held past the long-press threshold,
     * which is what OneButton's _state == OCS_PRESS meant. Read from the current
     * event rather than from a duration: the component does not reset its tick
     * counter when a long press is released, so a duration alone keeps reporting
     * the press that already ended. */
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
    // Only the double/multi-click disambiguation window now -- CLICK maps to
    // PRESS_UP, which does not wait on it. OneButton's _click_ms default, so a
    // double-click gesture is reachable at the cadence it was designed for.
    static constexpr uint16_t CLICK_RESOLVE_MS = 400;
    static_assert(CONFIG_BUTTON_DEBOUNCE_TICKS * CONFIG_BUTTON_PERIOD_TIME_MS == 50,
                  "debounce must stay at OneButton's 50ms; iot_button has no config field for it");
    static constexpr uint16_t LONG_PRESS_MS = 800;
    static constexpr uint16_t MULTI_CLICK_COUNT = 3;

    static button_event_t eventFor(ButtonInteraction interaction) {
        switch (interaction) {
            case ButtonInteraction::PRESS:             return BUTTON_PRESS_DOWN;
            // A completed press, which the component reports the moment the
            // button comes up. BUTTON_SINGLE_CLICK is a different thing: it
            // cannot fire until short_press_time has ruled out a second press,
            // so using it would make every click wait out the double-click
            // window. OneButton reported on release too.
            case ButtonInteraction::CLICK: return BUTTON_PRESS_UP;
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

        // The component raises PRESS_UP on every release, including the release
        // that ends a long press. OneButton sent that one to attachLongPressStop
        // and never to the click handler, so it is swallowed here rather than
        // counted as a click. Both writes and the read happen on the component's
        // timer task, in event order.
        if (event == BUTTON_LONG_PRESS_START) {
            driver->longPressSeen = true;
        } else if (event == BUTTON_PRESS_DOWN) {
            driver->longPressSeen = false;
        } else if (event == BUTTON_PRESS_UP && driver->longPressSeen) {
            return;
        }

        for (int i = 0; i < INTERACTION_COUNT; i++) {
            if (eventFor(static_cast<ButtonInteraction>(i)) == event) {
                driver->pending[i].fetch_add(1);
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
