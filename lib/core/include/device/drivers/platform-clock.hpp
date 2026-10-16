#pragma once

class PlatformClock {
public:
    virtual ~PlatformClock() = default;

    /**
     * Milliseconds since an arbitrary origin. Monotonic: an implementation owes a
     * reading never below the previous one, which a wall clock does not satisfy.
     * It wraps at the width of the return type, every 49.7 days on the device's
     * 32 bits, so callers measure an interval with plain unsigned subtraction,
     * which is exact across that wrap. A `now >= stamp` guard is not: it reads a
     * gap of 0 for the whole wrap period and disarms whatever it bounds (#242).
     */
    virtual unsigned long milliseconds() = 0;
};