#pragma once

#include <FastLED.h>

template<int pinNumber>
class LightStrip {
public:
    LightStrip(int numLights) {
        this->numLights = numLights;
        lights = new CRGB[numLights];
        FastLED.addLeds<WS2812B, pinNumber, GRB>(lights, numLights);

        lightMask = new CRGB[numLights];
        std::fill_n(lightMask, numLights, CRGB::White);
    }

    ~LightStrip() {
        delete []lights;
    }

    void setLight(int index, CRGB color) {
        lights[index] = color;
    };

    CRGB getLightColor(int index) {
        return lights[index];
    }

    void fade(int fadeAmount) {
        fadeToBlackBy(lights, numLights, fadeAmount);
    };

    void addToLight(int index, CRGB color) {
        lights[index] += color;
    };

    void loop() {
        for (int i = 0; i < numLights; i++) {
            lights[i] -= lightMask[i];
        }
    }

protected:
    int numLights;
    CRGB *lightMask;
    CRGB *lights;
    CRGBPalette16 currentPalette;
    int colorIndex;
    int brightness;
};
