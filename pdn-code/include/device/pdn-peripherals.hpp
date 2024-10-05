//
// Created by Elli Furedy on 10/5/2024.
//
#pragma once
#include <OneButton.h>

#include "device-interface/device-peripherals.hpp"

#include "display-lights.hpp"
#include "grip-lights.hpp"
#include "haptics.hpp"
#include "display.hpp"
#include "pdn-serial.hpp"

class PDNPeripherals : Peripherals {
public:
    PDNPeripherals();
    ~PDNPeripherals();

    void tick() override;

    OneButton getPrimaryButton();

    OneButton getSecondaryButton();

    Haptics getVibrator();

    Display getDisplay();

    DisplayLights getDisplayLights();

    GripLights getGripLights();

    PDNSerial getPDNSerial();

    void setGlobablLightColor(CRGB color);

    void setGlobalBrightness(int brightness);

private:
    Display display;
    OneButton primary;
    OneButton secondary;
    DisplayLights displayLights;
    GripLights gripLights;
    Haptics vibrationMotor;
    String deviceID = "";
    PDNSerial pdnSerial;
};
