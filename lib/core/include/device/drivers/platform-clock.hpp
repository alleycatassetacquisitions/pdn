#pragma once

#include <cstdint>

class PlatformClock {
public:
    virtual ~PlatformClock() = default;
    virtual unsigned long milliseconds() = 0;
    /** Returns UTC milliseconds since the Unix epoch. */
    virtual int64_t unixMilliseconds() {
        return 0;
    }
};