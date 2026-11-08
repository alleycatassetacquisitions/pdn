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

    /** Sets the UTC wall clock in milliseconds since the Unix epoch without affecting monotonic
     * milliseconds(). */
    virtual void setUnixMilliseconds(int64_t unixMs) { (void)unixMs; }
};