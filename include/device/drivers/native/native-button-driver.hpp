#pragma once

#include "device/drivers/driver-interface.hpp"
#include <map>

using ButtonCallbackMap = std::map<ButtonInteraction, callbackFunction>;
using ButtonParameterizedCallbackMap = std::map<ButtonInteraction, parameterizedCallbackFunction>;

class NativeButtonDriver : public ButtonDriverInterface {
    public:
    NativeButtonDriver(const std::string& name, int buttonPin) : ButtonDriverInterface(name) {}
    
    ~NativeButtonDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // No periodic execution needed for native button driver
    }

    bool isLongPressed() override {
        return false;
    }

    unsigned long longPressedMillis() override {
        return 0;
    }

    void setButtonPress(callbackFunction newFunction, ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        buttonCallbacks[interactionType] = newFunction;
    }

    void setButtonPress(parameterizedCallbackFunction newFunction, void* parameter, ButtonInteraction interactionType = ButtonInteraction::PRESS) override {
        buttonParameterizedCallbacks[interactionType] = newFunction;
    }

    void removeButtonCallbacks() override {
        buttonCallbacks.clear();
        buttonParameterizedCallbacks.clear();
    }

    bool hasCallback(ButtonInteraction interactionType) {
        return buttonCallbacks.find(interactionType) != buttonCallbacks.end() || buttonParameterizedCallbacks.find(interactionType) != buttonParameterizedCallbacks.end();
    }

    bool hasParameterizedCallback(ButtonInteraction interactionType) {
        return buttonParameterizedCallbacks.find(interactionType) != buttonParameterizedCallbacks.end();
    }

    void execCallback(ButtonInteraction interactionType) {
        if (hasCallback(interactionType)) {
            buttonCallbacks[interactionType]();
        }
    }

    void execParameterizedCallback(ButtonInteraction interactionType, void* parameter) {
        if (hasParameterizedCallback(interactionType)) {
            buttonParameterizedCallbacks[interactionType](parameter);
        }
    }
    
    private:
    ButtonCallbackMap buttonCallbacks = ButtonCallbackMap();
    ButtonParameterizedCallbackMap buttonParameterizedCallbacks = ButtonParameterizedCallbackMap();
};