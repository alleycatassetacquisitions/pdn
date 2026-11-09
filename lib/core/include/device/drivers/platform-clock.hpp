#pragma once

#include <cstdint>

class PlatformClock {
public:
    virtual ~PlatformClock() = default;

    /** Monotonic millisecond count since an arbitrary origin (wraps modulo ULONG_MAX + 1). */
    virtual unsigned long milliseconds() = 0;
    /** Returns UTC milliseconds since the Unix epoch. */
    virtual int64_t unixMilliseconds() {
        return 0;
    }
};