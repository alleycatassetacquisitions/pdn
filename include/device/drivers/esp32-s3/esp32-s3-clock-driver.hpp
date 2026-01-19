#pragma once

#include "device/drivers/driver-interface.hpp"
#include <Arduino.h>

class Esp32S3Clock : public PlatformClockDriverInterface {
public:
    Esp32S3Clock(std::string name) : PlatformClockDriverInterface(name) {
    }

    ~Esp32S3Clock() override {
    }


    int initialize() override {
        return 0;
    }

    void exec() override {
    }


    unsigned long milliseconds() override {
        return millis();
    }
};