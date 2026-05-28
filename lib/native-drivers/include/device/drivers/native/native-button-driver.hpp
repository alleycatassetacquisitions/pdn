#pragma once

#include "device/drivers/driver-interface.hpp"
#include <map>

// Structure to hold parameterized callback with its context
struct ParameterizedCallbackEntry {
    parameterizedCallbackFunction callback;
    void* context;
};

using ButtonCallbackMap = std::map<ButtonInteraction, callbackFunction>;
using ButtonParameterizedCallbackMap = std::map<ButtonInteraction, ParameterizedCallbackEntry>;

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
        // Store both the callback and its context
        buttonParameterizedCallbacks[interactionType] = {newFunction, parameter};
    }

    void removeButtonCallbacks() override {
        buttonCallbacks.clear();
        buttonParameterizedCallbacks.clear();
    }

    bool hasCallback(ButtonInteraction interactionType) {
        return buttonCallbacks.find(interactionType) != buttonCallbacks.end() || 
               buttonParameterizedCallbacks.find(interactionType) != buttonParameterizedCallbacks.end();
    }

    bool hasParameterizedCallback(ButtonInteraction interactionType) {
        return buttonParameterizedCallbacks.find(interactionType) != buttonParameterizedCallbacks.end();
    }

    /**
     * Execute any registered callback for the given interaction type.
     * This checks both non-parameterized and parameterized callbacks.
     * Parameterized callbacks are invoked with their stored context.
     */
    void execCallback(ButtonInteraction interactionType) {
        // Try non-parameterized callback first
        auto it = buttonCallbacks.find(interactionType);
        if (it != buttonCallbacks.end() && it->second) {
            it->second();
        }
        
        // Also try parameterized callback with stored context
        auto pit = buttonParameterizedCallbacks.find(interactionType);
        if (pit != buttonParameterizedCallbacks.end() && pit->second.callback) {
            pit->second.callback(pit->second.context);
        }
    }

    void execParameterizedCallback(ButtonInteraction interactionType, void* parameter) {
        auto it = buttonParameterizedCallbacks.find(interactionType);
        if (it != buttonParameterizedCallbacks.end() && it->second.callback) {
            it->second.callback(parameter);
        }
    }
    
    private:
    ButtonCallbackMap buttonCallbacks = ButtonCallbackMap();
    ButtonParameterizedCallbackMap buttonParameterizedCallbacks = ButtonParameterizedCallbackMap();
};