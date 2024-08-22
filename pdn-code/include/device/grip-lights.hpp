#pragma once

#include "device-constants.hpp"
#include "light-strip.hpp"

class GripLights : public LightStrip<gripLightsPin> {

    public:
        GripLights(int numLights) : LightStrip(numLights) {};

};