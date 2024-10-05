//
// Created by Elli Furedy on 10/5/2024.
//

#include "device/pdn-peripherals.hpp"


PDNPeripherals::PDNPeripherals() : display(displayCS, displayDC, displayRST),
                   vibrationMotor(motorPin),
                   primary(primaryButtonPin, true, true),
                   secondary(secondaryButtonPin, true, true),
                   displayLights(numDisplayLights),
                   gripLights(numGripLights) {}

OneButton PDNPeripherals::getPrimaryButton() {
    return primary;
}

OneButton PDNPeripherals::getSecondaryButton() {
    return secondary;
}

Haptics PDNPeripherals::getVibrator() {
    return vibrationMotor;
}

Display PDNPeripherals::getDisplay() {
    return display;
}

DisplayLights PDNPeripherals::getDisplayLights() {
    return displayLights;
}

GripLights PDNPeripherals::getGripLights() {
    return gripLights;
}

void PDNPeripherals::tick() {
    primary.tick();
    secondary.tick();
}

void PDNPeripherals::setGlobablLightColor(CRGB color) {
    FastLED.showColor(color, 255);
};

void PDNPeripherals::setGlobalBrightness(int brightness) {
    FastLED.setBrightness(brightness);
};