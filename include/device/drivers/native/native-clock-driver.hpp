#pragma once

#include "device/drivers/driver-interface.hpp"
#include <chrono>

class NativeClockDriver : public PlatformClockDriverInterface {
    public:
    explicit NativeClockDriver(const std::string& name) : PlatformClockDriverInterface(name) {
    }

    ~NativeClockDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // No periodic execution needed for clock driver
    }

    unsigned long milliseconds() override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() + offset_;
    }

    /*
     * Advance the clock by the given number of milliseconds.
     * Useful for testing timer-based state transitions without real delays.
     */
    void advance(unsigned long deltaMs) {
        offset_ += deltaMs;
    }

private:
    unsigned long offset_ = 0;
};