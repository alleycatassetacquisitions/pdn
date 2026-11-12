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

    /** Returns UTC milliseconds from std::chrono::system_clock (plus any synchronized offset). */
    int64_t unixMilliseconds() override {
        const int64_t sysMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        return sysMs + unixOffsetMs;
    }

    /** Sets the UTC wall clock in milliseconds since the Unix epoch without altering
     * milliseconds(). */
    void setUnixMilliseconds(int64_t unixMs) override {
        const int64_t sysMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        unixOffsetMs = unixMs - sysMs;
    }

private:
    int64_t unixOffsetMs = 0;
};