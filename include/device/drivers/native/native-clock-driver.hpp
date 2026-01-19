#pragma once

#include "device/drivers/driver-interface.hpp"
#include <chrono>

class NativeClockDriver : public PlatformClockDriverInterface {
    public:
    NativeClockDriver(std::string name) : PlatformClockDriverInterface(name) {
    }

    ~NativeClockDriver() override {
    }

    int initialize() override {
        return 0;
    }

    void exec() override {
    }

    unsigned long milliseconds() override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
};