#include "../include/device/light-strip.hpp"

LightStrip::LightStrip() {
    
}

void LightStrip::begin(int numLights, int pinNumber) {
    lightStrip = new CRGB[numLights];
    FastLED.addLeds<WS2812B, pinNumber, GRB>(lightStrip, numLights);
}