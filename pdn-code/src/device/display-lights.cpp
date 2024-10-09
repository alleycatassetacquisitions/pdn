#include "../include/device/display-lights.hpp"

void DisplayLights::setGraphRight(int value) {
    displayLightsOnOff[11] = value < 1;
    displayLightsOnOff[10] = value < 2;
    displayLightsOnOff[9] = value < 3;
    displayLightsOnOff[8] = value < 4;
    displayLightsOnOff[7] = value < 5;
    displayLightsOnOff[6] = value < 6;
}

void DisplayLights::setGraphLeft(int value) {
    displayLightsOnOff[0] = value < 1;
    displayLightsOnOff[1] = value < 2;
    displayLightsOnOff[2] = value < 3;
    displayLightsOnOff[3] = value < 4;
    displayLightsOnOff[4] = value < 5;
    displayLightsOnOff[5] = value < 6;
}

void DisplayLights::setTransmitLight(boolean on) {
    displayLightsOnOff[12] = on;
}

void DisplayLights::setTransmitLight(CRGB color) {
    displayLightsOnOff[12] = true;
    lights[12] = color;
}
