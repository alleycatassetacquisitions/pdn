//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once

#include <OneButton.h>
#include <string>

#include "device.hpp"
#include "display-lights.hpp"
#include "grip-lights.hpp"
#include "pdn-button.hpp"
#include "pdn-display.hpp"
#include "pdn-haptics.hpp"
#include "pdn-serial.hpp"

class PDN : public Device {

public:
    static PDN* GetInstance();

    ~PDN() override;

    int begin() override;

    void loop() override;

    void setDeviceId(string deviceId) override;

    string getDeviceId() override;

    void setGlobablLightColor(LEDColor color) override;

    void setGlobalBrightness(int brightness) override;

    void setButtonClick(ButtonInteraction interactionType, ButtonIdentifier whichButton, callbackFunction) override;

    void setButtonClick(ButtonInteraction interactionType, ButtonIdentifier whichButton,
        parameterizedCallbackFunction newFunction, void *parameter) override;

    void removeButtonCallbacks(ButtonIdentifier whichButton) override;

    bool isLongPressed(ButtonIdentifier whichButton) override;

    long longPressedMillis(ButtonIdentifier whichButton) override;

    void addToLight(LightIdentifier whichLights, int ledNum, LEDColor color) override;

    void fadeLightsBy(LightIdentifier whichLights, int value) override;

    Display * invalidateScreen() override;

    void render() override;

    Display * drawText(char *text, int xStart, int yStart) override;

    Display * drawImage(Image image) override;

    Display * drawImage(Image image, int xStart, int yStart) override;

    void setVibration(int value) override;

    int getCurrentVibrationIntensity() override;

protected:
    PDN();

    void initializePins() override;

    HWSerialWrapper* outputJack() override;

    HWSerialWrapper* inputJack() override;


private:
    PDNDisplay display;
    PDNHaptics haptics;
    PDNButton primary;
    PDNButton secondary;
    DisplayLights displayLights;
    GripLights gripLights;

    PDNSerialOut serialOut;
    PDNSerialIn serialIn;

    string deviceId;
};
