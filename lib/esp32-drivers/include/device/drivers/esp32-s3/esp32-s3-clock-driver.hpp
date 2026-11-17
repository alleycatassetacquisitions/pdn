#pragma once

#include "device/drivers/driver-interface.hpp"

#include <esp_timer.h>
#include <sys/time.h>

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
        // esp_timer counts microseconds since boot in 64 bits. The division only
        // converts units; narrowing to 32 bits is what makes this wrap where a
        // 32-bit millisecond count does.
        return static_cast<unsigned long>(esp_timer_get_time() / 1000);
    }

    /** Returns UTC milliseconds from the POSIX system clock. */
    int64_t unixMilliseconds() override {
        struct timeval tv = {};
        gettimeofday(&tv, nullptr);
        return static_cast<int64_t>(tv.tv_sec) * 1000 + static_cast<int64_t>(tv.tv_usec) / 1000;
    }
};
