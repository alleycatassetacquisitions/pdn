#pragma once

class PlatformClock {
public:
    virtual ~PlatformClock() = default;

    /** Monotonic millisecond count since an arbitrary origin (wraps modulo ULONG_MAX + 1). */
    virtual unsigned long milliseconds() = 0;
};