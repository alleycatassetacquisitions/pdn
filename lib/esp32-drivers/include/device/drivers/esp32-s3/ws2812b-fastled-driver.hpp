#pragma once

#include <FastLED.h>
#include "device/drivers/driver-interface.hpp"
#include "utils/simple-timer.hpp"

// DISPLAY_PIN and GRIP_PIN must be compile-time constants (required by FastLED template API).
// LED counts are passed at runtime via the constructor.
template<uint8_t DISPLAY_PIN, uint8_t GRIP_PIN>
class WS2812BFastLEDDriver : public LightDriverInterface {
public:
    WS2812BFastLEDDriver(const std::string& name, uint8_t numDisplayLights, uint8_t numGripLights)
        : LightDriverInterface(name),
          numDisplayLights(numDisplayLights),
          numGripLights(numGripLights) {
        displayLights = new CRGB[numDisplayLights];
        gripLights = new CRGB[numGripLights];
    }

    ~WS2812BFastLEDDriver() override {
        delete[] displayLights;
        delete[] gripLights;
    }

    int initialize() override {
        pinMode(DISPLAY_PIN, OUTPUT);
        pinMode(GRIP_PIN, OUTPUT);

        FastLED.addLeds<WS2812B, DISPLAY_PIN, GRB>(displayLights, numDisplayLights);
        FastLED.addLeds<WS2812B, GRIP_PIN, GRB>(gripLights, numGripLights);

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

    void setGlobalBrightness(uint8_t brightness) override {
        FastLED.setBrightness(brightness);
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
            fadeToBlackBy(displayLights, numDisplayLights, fadeAmount);
        } else if(lightSet == LightIdentifier::GRIP_LIGHTS) {
            fadeToBlackBy(gripLights, numGripLights, fadeAmount);
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
    uint8_t numDisplayLights;
    uint8_t numGripLights;
    SimpleTimer frameTimer;

    const uint8_t DEFAULT_FPS = 60;
    uint8_t fps;

};
