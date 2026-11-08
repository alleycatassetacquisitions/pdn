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
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    /** Returns UTC milliseconds from std::chrono::system_clock. */
    int64_t unixMilliseconds() override { return systemUnixMilliseconds() + unixOffsetMs; }

    /** Offsets the simulated wall clock; the host clock is never touched. */
    void setUnixMilliseconds(int64_t unixMs) override {
        unixOffsetMs = unixMs - systemUnixMilliseconds();
    }

private:
    static int64_t systemUnixMilliseconds() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    int64_t unixOffsetMs = 0;
};