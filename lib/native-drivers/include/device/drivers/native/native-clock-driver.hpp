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
        // steady_clock, not system_clock: PlatformClock promises monotonic, and a
        // wall clock steps backwards on an NTP step or a manual date change. Every
        // elapsed-time site in lib/core now reads such a step as a near-max gap,
        // which on this driver would tear down live links in the sim (#242).
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};