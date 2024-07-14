#pragma once

#include <OneButton.h>
#include <U8g2lib.h>
#include <FastLED.h>
#include "light-strip.hpp"

#define primaryButtonPin 15
#define secondaryButtonPin 16
#define motorPin 17
#define RXr 41
#define RXt 40
#define TXt 39 //MIDDLE BAND ON AUDIO CABLE
#define TXr 38 //TIP OF AUDIO CABLE
#define displayLightsPin 13
#define gripLightsPin 21
#define displayCS 10
#define displayDC 9
#define displayRST 14

#define numDisplayLights 13
#define numGripLights 6

class Device {

    public:
        const int BAUDRATE = 19200;
        

    private:
        U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display;
        OneButton primary;
        OneButton secondary;
        CRGB displayLights[numDisplayLights];
        CRGB gripLights[numGripLights];
    

};