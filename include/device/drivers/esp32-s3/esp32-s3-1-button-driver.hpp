//
// Created by Elli Furedy on 10/13/2024.
//
#pragma once
#include <OneButton.h>

#include "device/drivers/driver-interface.hpp"

class Esp32S31ButtonDriver : public ButtonDriverInterface {
    public:
    Esp32S31ButtonDriver(const std::string& name, int buttonPin) 
        : ButtonDriverInterface(name), button_(buttonPin, true, true) {}

    ~Esp32S31ButtonDriver() override = default;

    int initialize() override {
        return 0;
    }

    void setButtonPress(callbackFunction newFunction, ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        switch(interactionType) {
            case ButtonInteraction::PRESS: {
                button_.attachPress(newFunction);
                break;
            }
            case ButtonInteraction::CLICK: {
                button_.attachClick(newFunction);
                break;
            }
            case ButtonInteraction::DOUBLE_CLICK: {
                button_.attachDoubleClick(newFunction);
                break;
            }
            case ButtonInteraction::MULTI_CLICK: {
                button_.attachMultiClick(newFunction);
                break;
            }
            case ButtonInteraction::LONG_PRESS: {
                button_.attachLongPressStart(newFunction);
                break;
            }
            case ButtonInteraction::DURING_LONG_PRESS: {
                button_.attachDuringLongPress(newFunction);
                break;
            }
            case ButtonInteraction::RELEASE: {
                button_.attachLongPressStop(newFunction);
                break;
            }
        }
    }

    void setButtonPress(parameterizedCallbackFunction newFunction, void* parameter, ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        switch(interactionType) {
            case ButtonInteraction::PRESS: {
                button_.attachPress(newFunction, parameter);
                break;
            }
            case ButtonInteraction::CLICK: {
                button_.attachClick(newFunction, parameter);
                break;
            }
            case ButtonInteraction::DOUBLE_CLICK: {
                button_.attachDoubleClick(newFunction, parameter);
                break;
            }
            case ButtonInteraction::MULTI_CLICK: {
                button_.attachMultiClick(newFunction, parameter);
                break;
            }
            case ButtonInteraction::LONG_PRESS: {
                button_.attachLongPressStart(newFunction, parameter);
                break;
            }
            case ButtonInteraction::DURING_LONG_PRESS: {
                button_.attachDuringLongPress(newFunction, parameter);
                break;
            }
            case ButtonInteraction::RELEASE: {
                button_.attachLongPressStop(newFunction, parameter);
                break;
            }
        }
    }

    void removeButtonCallbacks() override {
        button_.reset();
    }

    void exec() override {
        button_.tick();
    }

    bool isLongPressed() override {
        return button_.isLongPressed();
    }

    unsigned long longPressedMillis() override {
        return button_.getPressedMs();
    }

private:
    OneButton button_;
};