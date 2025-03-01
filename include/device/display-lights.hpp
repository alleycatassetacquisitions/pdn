#pragma once

#include "light-strip.hpp"
#include "device-constants.hpp"

class DisplayLights : public LightStrip<displayLightsPin> {
public:
    DisplayLights(int numLights) : LightStrip(numLights) {
    };
};
