#include "Arduino.h"
#include "../include/device/haptics.hpp"

Haptics::Haptics(int pin)
{
    pinNumber = pin;
    intensity = 0;
    active = false;
}

bool Haptics::isOn()
{
    return active;
}

void Haptics::max()
{
    intensity = 255;
    active = true;
    analogWrite(pinNumber, 255);
}

void Haptics::setIntensity(int intensity)
{
    if(intensity > 255) this->intensity = 255;
    else if(intensity < 0) this->intensity = 0;
    else this->intensity = intensity;

    analogWrite(pinNumber, intensity);
}

void Haptics::off()
{
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
