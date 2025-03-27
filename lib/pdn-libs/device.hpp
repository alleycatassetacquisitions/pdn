#pragma once

#include <string>
#include "device-serial.hpp"
#include "display.hpp"
#include "button.hpp"
#include "light-interface.hpp"

using namespace std;

//LED Resource Identifiers

class Device : public DeviceSerial {
public:
    ~Device() override {}

    virtual int begin() = 0;

    virtual void loop() = 0;

    virtual void onStateChange() = 0;

    virtual void setDeviceId(string deviceId) = 0;

    virtual string getDeviceId() = 0;

    //Button Methods
    virtual void setButtonClick(ButtonInteraction interactionType, ButtonIdentifier whichButton, callbackFunction) = 0;

    virtual void setButtonClick(ButtonInteraction interactionType, ButtonIdentifier whichButton, parameterizedCallbackFunction newFunction, void *parameter) = 0;

    virtual void removeButtonCallbacks(ButtonIdentifier whichButton) = 0;

    virtual bool isLongPressed(ButtonIdentifier whichButton) = 0;

    virtual unsigned long longPressedMillis(ButtonIdentifier whichButton) = 0;

    // LED Methods
    virtual void setGlobalLightColor(LEDColor color) = 0;

    virtual void setGlobalBrightness(int brightness) = 0;

    virtual void addToLight(LightIdentifier whichLights, int ledNum, LEDColor color) = 0;

    virtual void fadeLightsBy(LightIdentifier whichLights, int value) = 0;

    virtual void setLight(LightIdentifier whichLights, int ledNum, LEDColor color) = 0;

    //Vibration Motor Methods
    virtual void setVibration(int value) = 0;

    virtual int getCurrentVibrationIntensity() = 0;

    //Display Methods
    virtual Display* invalidateScreen() = 0;

    virtual void render() = 0;

    virtual Display* drawText(const char *text) = 0;

    virtual Display* drawText(const char *text, int xStart, int yStart) = 0;

    virtual Display* setGlyphMode(FontMode mode) = 0;

    virtual Display* renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) = 0;

    virtual Display* drawImage(Image image) = 0;

    virtual Display* drawImage(Image image, int xStart, int yStart) = 0;

    // Animation control methods
    virtual void startAnimation(AnimationConfig config) = 0;
    virtual void stopAnimation() = 0;
    virtual void pauseAnimation() = 0;
    virtual void resumeAnimation() = 0;
    virtual bool isAnimating() const = 0;
    virtual bool isPaused() const = 0;
    virtual bool isAnimationComplete() const = 0;
    virtual AnimationType getCurrentAnimation() const = 0;

protected:
    Device() {}

    virtual void initializePins() = 0;
};
