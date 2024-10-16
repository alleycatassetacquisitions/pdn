//
// Created by Elli Furedy on 10/13/2024.
//
#pragma once
#include <OneButton.h>

#include "button.hpp"

class PDNButton : public Button {
    public:
    explicit PDNButton(int buttonPin);

    ~PDNButton() override;

    void setButtonPress(callbackFunction newFunction) override;

    void setButtonSingleClick(callbackFunction newFunction) override;

    void setButtonDoubleClick(callbackFunction newFunction) override;

    void setButtonMultiClick(callbackFunction newFunction) override;

    void setButtonLongPress(callbackFunction newFunction) override;

    void setButtonDuringLongPress(callbackFunction newFunction) override;

    void setButtonLongPressRelease(callbackFunction newFunction) override;

    void setButtonPress(parameterizedCallbackFunction newFunction, void *parameter) override;

    void setButtonSingleClick(parameterizedCallbackFunction newFunction, void *parameter) override;

    void setButtonDoubleClick(parameterizedCallbackFunction newFunction, void *parameter) override;

    void setButtonMultiClick(parameterizedCallbackFunction newFunction, void *parameter) override;

    void setButtonLongPress(parameterizedCallbackFunction newFunction, void *parameter) override;

    void setButtonDuringLongPress(parameterizedCallbackFunction newFunction, void *parameter) override;

    void setButtonLongPressRelease(parameterizedCallbackFunction newFunction, void *parameter) override;

    void removeButtonCallbacks() override;

    void loop() override;

    bool isLongPressed() override;

    unsigned long longPressedMillis() override;

private:
    OneButton button;
};