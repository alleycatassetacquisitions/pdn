#pragma once

#include <FastLED.h>
#include "device/drivers/driver-interface.hpp"
#include "device/device-constants.hpp"
#include "utils/simple-timer.hpp"

struct LightStripConfig {
    LightIdentifier lightSet;
    uint8_t numLights;

    LightStripConfig(LightIdentifier lightSet = LightIdentifier::GLOBAL, uint8_t numLights = 0) : lightSet(lightSet), numLights(numLights) {}
};

const LightStripConfig RecessLightsConfig(LightIdentifier::RECESS_LIGHTS, numRecessLights);
const LightStripConfig FinLightsConfig(LightIdentifier::FIN_LIGHTS, numFinLights);

class WS2812BFastLEDDriver : public LightDriverInterface {
public:
    explicit WS2812BFastLEDDriver(const std::string& name) : LightDriverInterface(name) {
        recessLights = new CRGB[RecessLightsConfig.numLights];
        finLights = new CRGB[FinLightsConfig.numLights];
    };

    ~WS2812BFastLEDDriver() override {
        delete[] recessLights;
        delete[] finLights;
    };

    int initialize() override {
        pinMode(recessLightsPin, OUTPUT);
        pinMode(finLightsPin, OUTPUT);

        FastLED.addLeds<WS2812B, recessLightsPin, GRB>(recessLights, RecessLightsConfig.numLights);
        FastLED.addLeds<WS2812B, finLightsPin, GRB>(finLights, FinLightsConfig.numLights);

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
        if(lightSet == LightIdentifier::RECESS_LIGHTS) {
            recessLights[index] = CRGB(color.color.red, color.color.green, color.color.blue);
            recessLights[index].nscale8(color.brightness);
        } else if(lightSet == LightIdentifier::FIN_LIGHTS) {
            finLights[index] = CRGB(color.color.red, color.color.green, color.color.blue);
            finLights[index].nscale8(color.brightness);
        }
    };

    void setLightBrightness(LightIdentifier lightSet, uint8_t index, uint8_t brightness) {
        if(lightSet == LightIdentifier::RECESS_LIGHTS) {
            recessLights[index].nscale8(brightness);
        } else if(lightSet == LightIdentifier::FIN_LIGHTS) {
            finLights[index].nscale8(brightness);
        }
    }

    void setGlobalBrightness(uint8_t brightness) override {
        FastLED.setBrightness(brightness);
    }

    LEDState::SingleLEDState getLight(LightIdentifier lightSet, uint8_t index) override {
        if(lightSet == LightIdentifier::RECESS_LIGHTS) {
            return LEDState::SingleLEDState(
                LEDColor(recessLights[index].r, recessLights[index].g, recessLights[index].b), 255);
        } else if(lightSet == LightIdentifier::FIN_LIGHTS) {
            return LEDState::SingleLEDState(
                LEDColor(finLights[index].r, finLights[index].g, finLights[index].b), 255);
        }
        return LEDState::SingleLEDState();
    }
    
    void fade(LightIdentifier lightSet, uint8_t fadeAmount) override {
        if(lightSet == LightIdentifier::RECESS_LIGHTS) {
            fadeToBlackBy(recessLights, RecessLightsConfig.numLights, fadeAmount);
        } else if(lightSet == LightIdentifier::FIN_LIGHTS) {
            fadeToBlackBy(finLights, FinLightsConfig.numLights, fadeAmount);
        }
    }
    
    void addToLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {
        if(lightSet == LightIdentifier::RECESS_LIGHTS) {
            recessLights[index] += CRGB(color.color.red, color.color.green, color.color.blue);
            recessLights[index].nscale8(color.brightness);
        } else if(lightSet == LightIdentifier::FIN_LIGHTS) {
            finLights[index] += CRGB(color.color.red, color.color.green, color.color.blue);
            finLights[index].nscale8(color.brightness);
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
    CRGB* recessLights;
    CRGB* finLights;
    SimpleTimer frameTimer;

    const uint8_t DEFAULT_FPS = 60;
    uint8_t fps;

};
