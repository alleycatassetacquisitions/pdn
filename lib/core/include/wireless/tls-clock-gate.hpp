#pragma once

#include "utils/debounced-condition.hpp"
#include "utils/utc-clock-range.hpp"

/**
 * Holds HTTPS requests until the wall clock is set. Certificate dates are
 * checked against that clock, which reads 1970 after a cold boot until SNTP
 * sets it, so a request sent earlier would fail verification.
 */
class TlsClockGate {
public:
    // Covers SNTP's randomised startup delay (up to 5 s) and a slow answer.
    static constexpr unsigned long CLOCK_WAIT_MS = 10000;

    enum class Decision { SEND, HOLD, GIVE_UP };

    /**
     * Decides for the request at the head of the queue: SEND once the clock is
     * set, HOLD while it is not, GIVE_UP once that request has been held for
     * CLOCK_WAIT_MS. The next held request starts a wait of its own.
     */
    Decision check() {
        const bool clockSet = isClockSet();
        if (clockUnset.heldFor(!clockSet, CLOCK_WAIT_MS)) {
            clockUnset.reset();
            return Decision::GIVE_UP;
        }
        return clockSet ? Decision::SEND : Decision::HOLD;
    }

    /** Drops a wait in progress, for when the held request is gone. */
    void reset() { clockUnset.reset(); }

    /** Whether the wall clock reads a time from 2025 on, as only a set clock does. */
    static bool isClockSet() {
        PlatformClock* clock = SimpleTimer::getPlatformClock();
        return clock != nullptr && isValidUtcUnixMs(clock->unixMilliseconds());
    }

private:
    DebouncedCondition clockUnset;
};
