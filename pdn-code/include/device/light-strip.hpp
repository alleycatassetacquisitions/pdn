#pragma once

#include <FastLED.h>

class LightStrip {

    public:
        LightStrip();
        void begin(
            int numLights,
            int pinNumber
        );

    protected:
        CRGB* lightStrip;
        CRGBPalette16 currentPalette;
        int colorIndex;
        int brightness;
};