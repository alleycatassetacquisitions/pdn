#pragma once
#include <string>
#include "device-constants.hpp"
#include "device-serial.hpp"
#include "image.hpp"

using namespace std;

enum {
    GLOBAL = 0,
    DISPLAY_LIGHTS = 1,
    GRIP_LIGHTS = 2
};

enum {
    PRIMARY_BUTTON = 0,
    SECONDARY_BUTTON = 1,
};

struct PDNColor {
    int red;
    int green;
    int blue;

    PDNColor(int red, int green, int blue) {
        this->red = red;
        this->green = green;
        this->blue = blue;
    }
};

typedef void (*parameterizedCallbackFunction)(void *);

class Device : public DeviceSerial {
public:
    ~Device() override {};

    virtual int begin() = 0;

    virtual void tick() = 0;

    virtual void setDeviceId(string deviceId) = 0;

    virtual string getDeviceId() = 0;

    virtual void setButtonClick(int whichButton, parameterizedCallbackFunction newFunction, void *parameter) = 0;

    virtual void removeButtonCallbacks() = 0;

    virtual void setGlobablLightColor(PDNColor color) = 0;

    virtual void setGlobalBrightness(int brightness) = 0;

    virtual void addToLight(int whichLights, int ledNum, PDNColor color) = 0;

    virtual void fadeLightsBy(int whichLights, int value) = 0;

    virtual void setVibration(int value) = 0;

    virtual int getCurrentVibrationIntensity() = 0;

    virtual void drawText(char *text, int xStart, int yStart) = 0;

    virtual void drawImage(Image image, int xStart, int yStart) = 0;

protected:
    Device() {}

    virtual void initializePins() = 0;
};
