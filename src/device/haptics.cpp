#include "Arduino.h"
#include "device/pdn-haptics.hpp"

PDNHaptics::PDNHaptics(int pin) {
    pinNumber = pin;
    intensity = 0;
    active = false;

    pinMode(pinNumber, OUTPUT);
}

bool PDNHaptics::isOn() {
    return active;
}

void PDNHaptics::max() {
    intensity = 255;
    active = true;
    analogWrite(pinNumber, 255);
}

int PDNHaptics::getIntensity() {
    return intensity;
}

void PDNHaptics::setIntensity(int intensity) {
    if (intensity > 255) this->intensity = 255;
    else if (intensity < 0) this->intensity = 0;
    else this->intensity = intensity;

    analogWrite(pinNumber, intensity);
}

void PDNHaptics::off() {
    intensity = 0;
    active = false;
    analogWrite(pinNumber, 0);
}

// void Haptics::loadPattern(HapticsPattern pattern)
// {
//     currentPattern = pattern;
// }

// int Haptics::playPattern()
// {
//     return 0;
// }
