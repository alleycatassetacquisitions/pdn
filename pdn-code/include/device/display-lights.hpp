#pragma once

#include "light-strip.hpp"
#include "device-constants.hpp"

class DisplayLights : public LightStrip<displayLightsPin> {

    public:
        DisplayLights(int numLights) : LightStrip(numLights) {};

        void setGraphRight(int value);
        void setGraphLeft(int value);
        void setTransmitLight(boolean on);
        void setTransmitLight(CRGB color);

    private:
        boolean displayLightsOnOff[numDisplayLights] = {true, true, true, true, true,
                                                true, true, true, true, true,
                                                true, true, true};

};