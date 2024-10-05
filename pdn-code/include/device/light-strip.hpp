#pragma once

#include <FastLED.h>

template<int pinNumber>
class LightStrip {
public:
    LightStrip(int numLights) {
        this->numLights = numLights;
        lightStrip = new CRGB[numLights];
        FastLED.addLeds<WS2812B, pinNumber, GRB>(lightStrip, numLights);
    }

    ~LightStrip() {
        delete []lightStrip;
    }

    void setLight(int index, CRGB color) {
        lightStrip[index] = color;
    };

    void fade(int fadeAmount) {
        fadeToBlackBy(lightStrip, numLights, fadeAmount);
    };

    void addToLight(int index, CRGB color) {
        lightStrip[index] += color;
    };

protected:
    int numLights;
    CRGB *lightStrip;
    CRGBPalette16 currentPalette;
    int colorIndex;
    int brightness;
};
