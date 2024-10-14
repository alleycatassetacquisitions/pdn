//
// Created by Elli Furedy on 10/13/2024.
//
#include "device/pdn-button.hpp"

PDNButton::PDNButton(int buttonPin) : button(buttonPin, true, true) {}

PDNButton::~PDNButton() {
    delete &button;
}

void PDNButton::setButtonPress(callbackFunction newFunction) {
    button.attachPress(newFunction);
}

void PDNButton::setButtonSingleClick(callbackFunction newFunction) {
    button.attachClick(newFunction);
}

void PDNButton::setButtonDoubleClick(callbackFunction newFunction) {
    button.attachDoubleClick(newFunction);
}

void PDNButton::setButtonMultiClick(callbackFunction newFunction) {
    button.attachMultiClick(newFunction);
}

void PDNButton::setButtonLongPress(callbackFunction newFunction) {
    button.attachLongPressStart(newFunction);
}

void PDNButton::setButtonDuringLongPress(callbackFunction newFunction) {
    button.attachDuringLongPress(newFunction);
}

void PDNButton::setButtonLongPressRelease(callbackFunction newFunction) {
    button.attachLongPressStop(newFunction);
}

void PDNButton::setButtonPress(parameterizedCallbackFunction newFunction, void *parameter) {
    button.attachPress(newFunction, parameter);
}

void PDNButton::setButtonSingleClick(parameterizedCallbackFunction newFunction, void *parameter) {
    button.attachClick(newFunction, parameter);
}

void PDNButton::setButtonDoubleClick(parameterizedCallbackFunction newFunction, void *parameter) {
    button.attachDoubleClick(newFunction, parameter);
}

void PDNButton::setButtonMultiClick(parameterizedCallbackFunction newFunction, void *parameter) {
    button.attachMultiClick(newFunction, parameter);
}

void PDNButton::setButtonLongPress(parameterizedCallbackFunction newFunction, void *parameter) {
    button.attachLongPressStart(newFunction, parameter);
}

void PDNButton::setButtonDuringLongPress(parameterizedCallbackFunction newFunction, void *parameter) {
    button.attachDuringLongPress(newFunction, parameter);
}

void PDNButton::setButtonLongPressRelease(parameterizedCallbackFunction newFunction, void *parameter) {
    button.attachLongPressStop(newFunction, parameter);
}

void PDNButton::removeButtonCallbacks() {
    button.reset();
}

void PDNButton::loop() {
    button.tick();
}

bool PDNButton::isLongPressed() {
    return button.isLongPressed();
}

unsigned long PDNButton::longPressedMillis() {
    return button.getPressedMs();
}
