#pragma once

#include "device/drivers/driver-interface.hpp"
#include <Arduino.h>

class Esp32S3Clock : public PlatformClockDriverInterface {
public:
    explicit Esp32S3Clock(const std::string& name) : PlatformClockDriverInterface(name) {
    }

    ~Esp32S3Clock() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // No periodic execution needed for clock driver
    }


    unsigned long milliseconds() override {
        return millis();
    }
};