#pragma once

#include "device-constants.hpp"
#include <OneButton.h>
#include <FastLED.h>
#include <functional>
#include "light-strip.hpp"
#include "display-lights.hpp"
#include "grip-lights.hpp"
#include "haptics.hpp"
#include "display.hpp"



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

        Haptics getVibrator();
        Display getDisplay();
        DisplayLights getDisplayLights();
        GripLights getGripLights();
        
        HardwareSerial outputJack(); 
        HardwareSerial inputJack();

        void clearComms();
        void flushComms();

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
        
        void initializePins();

};