#pragma once

#include <OneButton.h>
#include <FastLED.h>
#include "display-lights.hpp"
#include "grip-lights.hpp"
#include "haptics.hpp"
#include "display.hpp"


enum {
    OUTPUT_JACK = 1,
    INPUT_JACK = 2
};

class Device {
public:
    static Device *GetInstance();

    int begin();

    void tick();

    String getDeviceId();

    OneButton getPrimaryButton();

    OneButton getSecondaryButton();

    Haptics getVibrator();

    Display getDisplay();

    DisplayLights getDisplayLights();

    GripLights getGripLights();


    //Serial comms methods
    HardwareSerial outputJack();

    HardwareSerial inputJack();

    void writeString(String *msg);

    void writeString(const String *msg);

    String readString();

    void setActiveComms(int whichJack);

    String *peekComms();

    bool commsAvailable();

    int getSerialWriteQueueSize();

    void setGlobablLightColor(CRGB color);

    void setGlobalBrightness(int brightness);

private:
    Device();

    /*
     * I think these need to be wrapped in some kind of "PeripheralInterface" so that
     * we can inject mock peripherals in unit tests.
     */
    Display display;
    OneButton primary;
    OneButton secondary;
    DisplayLights displayLights;
    GripLights gripLights;
    Haptics vibrationMotor;
    String deviceID = "";

    //do we need a full blow serial comms manager? I think so.
    int currentCommsJack = 1;
    String head;

    void initializePins();
};
