//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once

#include <Arduino.h>

#include <OneButton.h>
#include <string>

#include "device-serial.hpp"
#include "../../lib/pdn-libs/device.hpp"
#include "display-lights.hpp"
#include "display.hpp"
#include "grip-lights.hpp"
#include "haptics.hpp"
#include "pdn-serial.hpp"

class PDN : public Device {

public:
    static PDN* GetInstance();

    ~PDN() override;

    int begin() override;

    void tick() override;

    void setDeviceId(string deviceId) override;

    string getDeviceId() override;

    void setButtonClick(int whichButton, parameterizedCallbackFunction newFunction, void *parameter) override;

    void removeButtonCallbacks() override;

    void setGlobablLightColor(PDNColor color) override;

    void setGlobalBrightness(int brightness) override;

    void addToLight(int whichLights, int ledNum, PDNColor color) override;

    void fadeLightsBy(int whichLights, int value) override;

    void setVibration(int value) override;

    int getCurrentVibrationIntensity() override;

protected:
    PDN();

    void initializePins() override;

    HWSerialWrapper* outputJack() override;

    HWSerialWrapper* inputJack() override;

    Display display;
    OneButton primary;
    OneButton secondary;
    DisplayLights displayLights;
    GripLights gripLights;
    Haptics vibrationMotor;

    PDNSerialOut serialOut;
    PDNSerialIn serialIn;

    string deviceId = "";
};
