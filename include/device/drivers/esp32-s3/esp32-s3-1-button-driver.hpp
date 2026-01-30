//
// Created by Elli Furedy on 10/13/2024.
//
#pragma once
#include <OneButton.h>

#include "device/drivers/driver-interface.hpp"

class Esp32S31ButtonDriver : public ButtonDriverInterface {
    public:
    Esp32S31ButtonDriver(const std::string& name, int buttonPin) 
        : ButtonDriverInterface(name), button(buttonPin, true, true) {}

    ~Esp32S31ButtonDriver() override = default;

    int initialize() override {
        return 0;
    }

    void setButtonPress(callbackFunction newFunction, ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        switch(interactionType) {
            case ButtonInteraction::PRESS: {
                button.attachPress(newFunction);
                break;
            }
            case ButtonInteraction::CLICK: {
                button.attachClick(newFunction);
                break;
            }
            case ButtonInteraction::DOUBLE_CLICK: {
                button.attachDoubleClick(newFunction);
                break;
            }
            case ButtonInteraction::MULTI_CLICK: {
                button.attachMultiClick(newFunction);
                break;
            }
            case ButtonInteraction::LONG_PRESS: {
                button.attachLongPressStart(newFunction);
                break;
            }
            case ButtonInteraction::DURING_LONG_PRESS: {
                button.attachDuringLongPress(newFunction);
                break;
            }
            case ButtonInteraction::RELEASE: {
                button.attachLongPressStop(newFunction);
                break;
            }
        }
    }

    void setButtonPress(parameterizedCallbackFunction newFunction, void* parameter, ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        switch(interactionType) {
            case ButtonInteraction::PRESS: {
                button.attachPress(newFunction, parameter);
                break;
            }
            case ButtonInteraction::CLICK: {
                button.attachClick(newFunction, parameter);
                break;
            }
            case ButtonInteraction::DOUBLE_CLICK: {
                button.attachDoubleClick(newFunction, parameter);
                break;
            }
            case ButtonInteraction::MULTI_CLICK: {
                button.attachMultiClick(newFunction, parameter);
                break;
            }
            case ButtonInteraction::LONG_PRESS: {
                button.attachLongPressStart(newFunction, parameter);
                break;
            }
            case ButtonInteraction::DURING_LONG_PRESS: {
                button.attachDuringLongPress(newFunction, parameter);
                break;
            }
            case ButtonInteraction::RELEASE: {
                button.attachLongPressStop(newFunction, parameter);
                break;
            }
        }
    }

    void removeButtonCallbacks() override {
        button.reset();
    }

    void exec() override {
        button.tick();
    }

    bool isLongPressed() override {
        return button.isLongPressed();
    }

    unsigned long longPressedMillis() override {
        return button.getPressedMs();
    }

private:
    OneButton button;
};