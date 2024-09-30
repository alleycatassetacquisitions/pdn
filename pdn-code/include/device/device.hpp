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
        static Device* GetInstance();
        int begin();
        void tick();
        String getDeviceId();

        void attachPrimaryButtonClick(callbackFunction click);
        void attachPrimaryButtonDoubleClick(callbackFunction doubleClick);
        void attachPrimaryButtonLongPress(callbackFunction longPress);
        
        void attachSecondaryButtonClick(callbackFunction click);
        void attachSecondaryButtonDoubleClick(callbackFunction doubleClick);
        void attachSecondaryButtonLongPress(callbackFunction longPress);

        OneButton getPrimaryButton();
        OneButton getSecondaryButton();

        Haptics getVibrator();
        Display getDisplay();
        DisplayLights getDisplayLights();
        GripLights getGripLights();


        //Serial comms methods
        HardwareSerial outputJack(); 
        HardwareSerial inputJack();

        void writeString(String* msg);
        void writeString(const String* msg);
        String readString();

        void setActiveComms(int whichJack);

        String* peekComms();
        bool commsAvailable();
        int getTrxBufferedMessagesSize();

        void setGlobablLightColor(CRGB color);
        void setGlobalBrightness(int brightness);
        

    private:

        Device();

        Display display;
        OneButton primary;
        OneButton secondary;
        DisplayLights displayLights;
        GripLights gripLights;
        Haptics vibrationMotor;
        String deviceID = "";

        const char STRING_TERM = '\r';
        const char STRING_START = '*';
        int currentCommsJack = 1;
        String head;

        void initializePins();

};