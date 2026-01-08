#pragma once

#include "platform-clock.hpp"
#include <Arduino.h>

class Esp32S3Clock : public PlatformClock {
public:
    Esp32S3Clock() {
    }

    unsigned long milliseconds() override {
        return millis();
    }
};