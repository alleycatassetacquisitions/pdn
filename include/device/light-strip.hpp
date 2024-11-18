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
        std::fill_n(lightMask, numLights, CRGB::Black);
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

    void setLightOn(int index, bool on) {
        if(on) {
            lightMask[index] = CRGB::Black;
        } else {
            lightMask[index] = CRGB::White;
        }
    }

    void resetMask() {
        for (int i = 0; i < numLights; i++) {
            lightMask[i] = CRGB::Black;
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
