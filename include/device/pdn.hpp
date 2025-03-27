//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once

#include "device.hpp"
#include "display-lights.hpp"
#include "grip-lights.hpp"
#include "pdn-button.hpp"
#include "pdn-display.hpp"
#include "pdn-haptics.hpp"
#include "pdn-serial.hpp"
#include "light-manager.hpp"

#include <string>

using namespace std;

class PDN : public Device {

public:
    static PDN* GetInstance();

    ~PDN() override;

    int begin() override;

    void loop() override;

    void onStateChange() override;

    void setDeviceId(string deviceId) override;

    string getDeviceId() override;

    void setGlobalLightColor(LEDColor color);

    void setGlobalBrightness(int brightness);

    void setButtonClick(ButtonInteraction interactionType, ButtonIdentifier whichButton, callbackFunction) override;

    void setButtonClick(ButtonInteraction interactionType, ButtonIdentifier whichButton,
        parameterizedCallbackFunction newFunction, void *parameter) override;

    void removeButtonCallbacks(ButtonIdentifier whichButton) override;

    bool isLongPressed(ButtonIdentifier whichButton) override;

    unsigned long longPressedMillis(ButtonIdentifier whichButton) override;

    void addToLight(LightIdentifier whichLights, int ledNum, LEDColor color);

    void fadeLightsBy(LightIdentifier whichLights, int value);

    void setLight(LightIdentifier whichLights, int ledNum, LEDColor color);

    Display * invalidateScreen() override;

    void render() override;

    Display * drawText(const char *text, int xStart, int yStart) override;

    Display* drawText(const char *text) override;

    Display * drawImage(Image image) override;

    Display * drawImage(Image image, int xStart, int yStart) override;

    Display* setGlyphMode(FontMode mode) override;

    Display* renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) override;
    void setVibration(int value) override;

    int getCurrentVibrationIntensity() override;

    // Animation control methods
    void startAnimation(AnimationConfig config);
    void stopAnimation();
    void pauseAnimation();
    void resumeAnimation();
    bool isAnimating() const;
    bool isPaused() const;
    bool isAnimationComplete() const;
    AnimationType getCurrentAnimation() const;

protected:
    PDN();

    void initializePins() override;

    HWSerialWrapper* outputJack() override;

    HWSerialWrapper* inputJack() override;

private:
    PDNButton* getButton(ButtonIdentifier whichButton);

    void attachSingleClick(ButtonIdentifier whichButton, callbackFunction newFunction);
    void attachSingleClick(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void* parameter);

    void attachDoubleClick(ButtonIdentifier whichButton, callbackFunction newFunction);
    void attachDoubleClick(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void* parameter);

    void attachMultiClick(ButtonIdentifier whichButton, callbackFunction newFunction);
    void attachMultiClick(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void* parameter);

    void attachPress(ButtonIdentifier whichButton, callbackFunction newFunction);
    void attachPress(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void* parameter);

    void attachLongPress(ButtonIdentifier whichButton, callbackFunction newFunction);
    void attachLongPress(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void* parameter);

    void attachDuringLongPress(ButtonIdentifier whichButton, callbackFunction newFunction);
    void attachDuringLongPress(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void* parameter);

    void attachLongPressRelease(ButtonIdentifier whichButton, callbackFunction newFunction);
    void attachLongPressRelease(ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void* parameter);

    PDNDisplay display;
    PDNHaptics haptics;
    PDNButton primary;
    PDNButton secondary;
    DisplayLights displayLights;
    GripLights gripLights;
    LightManager lightManager;

    PDNSerialOut serialOut;
    PDNSerialIn serialIn;

    string deviceId;
};
