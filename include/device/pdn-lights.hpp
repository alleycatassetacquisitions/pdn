#pragma once

#include <FastLED.h>
#include "light-interface.hpp"
#include "device-constants.hpp"

struct PDNLightStripConfig {
    LightIdentifier lightSet;
    uint8_t numLights;

    PDNLightStripConfig(LightIdentifier lightSet = LightIdentifier::GLOBAL, uint8_t numLights = 0) : lightSet(lightSet), numLights(numLights) {}
};

const PDNLightStripConfig DisplayLightsConfig(LightIdentifier::DISPLAY_LIGHTS, numDisplayLights);
const PDNLightStripConfig GripLightsConfig(LightIdentifier::GRIP_LIGHTS, numGripLights);

class PDNLightStrip : public LightStrip {
public:
    PDNLightStrip(PDNLightStripConfig config);

    ~PDNLightStrip();

    void setLight(uint8_t index, LEDState::SingleLEDState color) override;

    void setLightBrightness(uint8_t index, uint8_t brightness) override;

    LEDState::SingleLEDState getLight(uint8_t index) override;

    void fade(uint8_t fadeAmount) override;

    void addToLight(uint8_t index, LEDState::SingleLEDState color) override;

protected:
    PDNLightStripConfig config;
    CRGB *lights;
};
