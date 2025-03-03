#pragma once

#include <FastLED.h>

template<int pinNumber>
class LightStrip {
public:
    LightStrip(int numLights) {
        this->numLights = numLights;
        lights = new CRGB[numLights];
        FastLED.addLeds<WS2812B, pinNumber, GRB>(lights, numLights);
        // FastLED.setTemperature(DirectSunlight);
    }

    ~LightStrip() {
        delete []lights;
    }

    void setLight(int index, CRGB color) {
        lights[index] = color;
    };

    void setLightBrightness(int index, int brightness) {
        lights[index].nscale8(brightness);
    }

    CRGB getLightColor(int index) {
        return lights[index];
    }

    CRGB *getLight(int index) {
        return &lights[index];
    }

    void fade(int fadeAmount) {
        fadeToBlackBy(lights, numLights, fadeAmount);
    };

    void addToLight(int index, CRGB color) {
        lights[index] += color;
    };

protected:
    int numLights;
    CRGB *lights;
    CRGBPalette16 currentPalette;
    int colorIndex;
    int brightness;
};
