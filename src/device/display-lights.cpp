#include "../include/device/display-lights.hpp"

void DisplayLights::setLEDBarLeft(int value) {
    setLightOn(11, value < 1);
    setLightOn(10, value < 2);
    setLightOn(9, value < 3);
    setLightOn(8, value < 4);
    setLightOn(7, value < 5);
    setLightOn(6, value < 6);
}

void DisplayLights::setLEDBarRight(int value) {
    setLightOn(0, value < 1);
    setLightOn(1, value < 2);
    setLightOn(2, value < 3);
    setLightOn(3, value < 4);
    setLightOn(4, value < 5);
    setLightOn(5, value < 6);
}

void DisplayLights::setTransmitLight(boolean on) {
    setLightOn(12, on);
}

void DisplayLights::setTransmitLight(CRGB color) {
    setLightOn(12, true);
    lights[12] = color;
}
