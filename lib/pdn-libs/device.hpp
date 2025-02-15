#pragma once
#include <string>
#include "device-serial.hpp"
#include "display.hpp"
#include "button.hpp"

using namespace std;

//LED Resource Identifiers

enum class LightIdentifier {
    GLOBAL = 0,
    DISPLAY_LIGHTS = 1,
    GRIP_LIGHTS = 2,
    TRANSMIT_LIGHT = 3,
    LEFT_LIGHTS = 4,
    RIGHT_LIGHTS = 5
};

struct LEDColor {
    int red;
    int green;
    int blue;

    LEDColor(int red, int green, int blue) {
        this->red = red;
        this->green = green;
        this->blue = blue;
    }
};

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
    virtual void setGlobablLightColor(LEDColor color) = 0;

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

    virtual Display* drawImage(Image image) = 0;

    virtual Display* drawImage(Image image, int xStart, int yStart) = 0;

protected:
    Device() {}

    virtual void initializePins() = 0;
};
