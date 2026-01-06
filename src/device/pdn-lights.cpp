#include "device/pdn-lights.hpp"
#include "device-constants.hpp"

PDNLightStrip::PDNLightStrip(PDNLightStripConfig config) {
    this->config = config;
    lights = new CRGB[config.numLights];
    if(config.lightSet == LightIdentifier::DISPLAY_LIGHTS) {
        FastLED.addLeds<WS2812B, displayLightsPin, GRB>(lights, config.numLights);
    } else if(config.lightSet == LightIdentifier::GRIP_LIGHTS) {
        FastLED.addLeds<WS2812B, gripLightsPin, GRB>(lights, config.numLights);
    }
}

PDNLightStrip::~PDNLightStrip() {
    delete[] lights;
}

void PDNLightStrip::setLight(uint8_t index, LEDState::SingleLEDState color) {
    lights[index] = CRGB(color.color.red, color.color.green, color.color.blue);
    lights[index].nscale8(color.brightness);
}

void PDNLightStrip::setLightBrightness(uint8_t index, uint8_t brightness) {
    lights[index].nscale8(brightness);
}

LEDState::SingleLEDState PDNLightStrip::getLight(uint8_t index) {
    return LEDState::SingleLEDState(LEDColor(lights[index].r, lights[index].g), lights[index].b);
}

void PDNLightStrip::fade(uint8_t fadeAmount) {
    fadeToBlackBy(lights, config.numLights, fadeAmount);
}

void PDNLightStrip::addToLight(uint8_t index, LEDState::SingleLEDState color) {
    lights[index] += CRGB(color.color.red, color.color.green, color.color.blue);
    lights[index].nscale8(color.brightness);
}