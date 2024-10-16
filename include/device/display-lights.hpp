#pragma once

#include "light-strip.hpp"
#include "device-constants.hpp"

class DisplayLights : public LightStrip<displayLightsPin> {
public:
    DisplayLights(int numLights) : LightStrip(numLights) {
    };

    void setLEDBarLeft(int value);

    void setLEDBarRight(int value);

    void setTransmitLight(boolean on);

    void setTransmitLight(CRGB color);

private:
    boolean displayLightsOnOff[numDisplayLights] = {
        true, true, true, true, true,
        true, true, true, true, true,
        true, true, true
    };
};
