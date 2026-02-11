#pragma once

#include "device/drivers/driver-interface.hpp"
#include <chrono>

class NativeClockDriver : public PlatformClockDriverInterface {
    public:
    explicit NativeClockDriver(const std::string& name) : PlatformClockDriverInterface(name) {
        baseTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    ~NativeClockDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // No periodic execution needed for clock driver
    }

    unsigned long milliseconds() override {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now + offset_;
    }

    /*
     * Advance the clock by the given number of milliseconds.
     * Used by tests to simulate time passing without waiting.
     */
    void advance(unsigned long ms) {
        offset_ += ms;
    }

private:
    unsigned long baseTime_ = 0;
    unsigned long offset_ = 0;
};