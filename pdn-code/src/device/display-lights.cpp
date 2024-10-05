#include "../include/device/display-lights.hpp"

void DisplayLights::setGraphRight(int value) {
    displayLightsOnOff[11] = value < 4;
    displayLightsOnOff[10] = value < 5;
    displayLightsOnOff[9] = value < 6;
    displayLightsOnOff[8] = value < 7;
    displayLightsOnOff[7] = value < 8;
    displayLightsOnOff[6] = value < 9;
}

void DisplayLights::setGraphLeft(int value) {
    displayLightsOnOff[0] = value < 4;
    displayLightsOnOff[1] = value < 5;
    displayLightsOnOff[2] = value < 6;
    displayLightsOnOff[3] = value < 7;
    displayLightsOnOff[4] = value < 8;
    displayLightsOnOff[5] = value < 9;
}

void DisplayLights::setTransmitLight(boolean on) {
    displayLightsOnOff[12] = on;
}

void DisplayLights::setTransmitLight(CRGB color) {
    displayLightsOnOff[12] = true;
    lightStrip[12] = color;
}
