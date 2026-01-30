//
// Created by Elli Furedy on 10/13/2024.
//
#pragma once

#include <functional>

//Button Resources
enum class ButtonIdentifier {
    PRIMARY_BUTTON = 0,
    SECONDARY_BUTTON = 1,
};

enum class ButtonInteraction {
    PRESS = 0,
    CLICK = 1,
    DOUBLE_CLICK = 2,
    MULTI_CLICK = 3,
    LONG_PRESS = 4,
    DURING_LONG_PRESS = 5,
    RELEASE = 6
};

typedef void (*callbackFunction)();
typedef void (*parameterizedCallbackFunction)(void *);

class Button {
    public:
    virtual ~Button() {}

    virtual void setButtonPress(callbackFunction newFunction, ButtonInteraction interactionType = ButtonInteraction::PRESS) = 0;
    
    virtual void setButtonPress(parameterizedCallbackFunction newFunction, void* parameter, ButtonInteraction interactionType = ButtonInteraction::PRESS) = 0;
    
    virtual void removeButtonCallbacks() = 0;

    virtual bool isLongPressed() = 0;

    virtual unsigned long longPressedMillis() = 0;
};
