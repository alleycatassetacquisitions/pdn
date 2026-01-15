#pragma once

#include <FastLED.h>
#include "driver-interface.hpp"
#include "device-constants.hpp"
#include "utils/simple-timer.hpp"

struct PDNLightStripConfig {
    LightIdentifier lightSet;
    uint8_t numLights;

    PDNLightStripConfig(LightIdentifier lightSet = LightIdentifier::GLOBAL, uint8_t numLights = 0) : lightSet(lightSet), numLights(numLights) {}
};

const PDNLightStripConfig DisplayLightsConfig(LightIdentifier::DISPLAY_LIGHTS, numDisplayLights);
const PDNLightStripConfig GripLightsConfig(LightIdentifier::GRIP_LIGHTS, numGripLights);

class WS2812BFastLEDDriver : public LightDriverInterface {
public:
    WS2812BFastLEDDriver(std::string name) : LightDriverInterface(name) {
        displayLights = new CRGB[DisplayLightsConfig.numLights];
        gripLights = new CRGB[GripLightsConfig.numLights];
    };

    ~WS2812BFastLEDDriver() override {
        delete[] displayLights;
        delete[] gripLights;
    };

    int initialize() override {
        pinMode(displayLightsPin, OUTPUT);
        pinMode(gripLightsPin, OUTPUT);

        FastLED.addLeds<WS2812B, displayLightsPin, GRB>(displayLights, DisplayLightsConfig.numLights);
        FastLED.addLeds<WS2812B, gripLightsPin, GRB>(gripLights, GripLightsConfig.numLights);

        setFPS(DEFAULT_FPS);

        return 0;
    }

    void exec() override {
        if(frameTimer.expired()) {
            frameTimer.setTimer(1000 / fps);
            FastLED.show();
        }
    }

    void setLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {
        if(lightSet == LightIdentifier::DISPLAY_LIGHTS) {
            displayLights[index] = CRGB(color.color.red, color.color.green, color.color.blue);
            displayLights[index].nscale8(color.brightness);
        } else if(lightSet == LightIdentifier::GRIP_LIGHTS) {
            gripLights[index] = CRGB(color.color.red, color.color.green, color.color.blue);
            gripLights[index].nscale8(color.brightness);
        }
    };

    void setLightBrightness(LightIdentifier lightSet, uint8_t index, uint8_t brightness) {
        if(lightSet == LightIdentifier::DISPLAY_LIGHTS) {
            displayLights[index].nscale8(brightness);
        } else if(lightSet == LightIdentifier::GRIP_LIGHTS) {
            gripLights[index].nscale8(brightness);
        }
    }
    
    LEDState::SingleLEDState getLight(LightIdentifier lightSet, uint8_t index) override {
        if(lightSet == LightIdentifier::DISPLAY_LIGHTS) {
            return LEDState::SingleLEDState(LEDColor(displayLights[index].r, displayLights[index].g), displayLights[index].b);
        } else if(lightSet == LightIdentifier::GRIP_LIGHTS) {
            return LEDState::SingleLEDState(LEDColor(gripLights[index].r, gripLights[index].g), gripLights[index].b);
        }
    }
    
    void fade(LightIdentifier lightSet, uint8_t fadeAmount) override {
        if(lightSet == LightIdentifier::DISPLAY_LIGHTS) {
            fadeToBlackBy(displayLights, DisplayLightsConfig.numLights, fadeAmount);
        } else if(lightSet == LightIdentifier::GRIP_LIGHTS) {
            fadeToBlackBy(gripLights, GripLightsConfig.numLights, fadeAmount);
        }
    }
    
    void addToLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {
        if(lightSet == LightIdentifier::DISPLAY_LIGHTS) {
            displayLights[index] += CRGB(color.color.red, color.color.green, color.color.blue);
            displayLights[index].nscale8(color.brightness);
        } else if(lightSet == LightIdentifier::GRIP_LIGHTS) {
            gripLights[index] += CRGB(color.color.red, color.color.green, color.color.blue);
            gripLights[index].nscale8(color.brightness);
        }
    }

    void setFPS(uint8_t fps) override {
        this->fps = fps;
        frameTimer.setTimer(1000 / fps);
    }

    uint8_t getFPS() const override {
        return fps;
    }

protected:
    CRGB* displayLights;
    CRGB* gripLights;
    SimpleTimer frameTimer;

    const uint8_t DEFAULT_FPS = 60;
    uint8_t fps;

};
