#pragma once

#include "utils/http-date.hpp"
#include "utils/simple-timer.hpp"

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
        if (isClockSet()) {
            waitTimer.invalidate();
            return Decision::SEND;
        }
        if (!waitTimer.isRunning()) {
            waitTimer.setTimer(CLOCK_WAIT_MS);
        }
        if (!waitTimer.expired()) {
            return Decision::HOLD;
        }
        waitTimer.invalidate();
        return Decision::GIVE_UP;
    }

    /** Drops a wait in progress, for when the held request is gone. */
    void reset() { waitTimer.invalidate(); }

    /** Whether the wall clock reads a time from 2025 on, as only a set clock does. */
    static bool isClockSet() {
        PlatformClock* clock = SimpleTimer::getPlatformClock();
        return clock != nullptr && isValidUtcUnixMs(clock->unixMilliseconds());
    }

private:
    SimpleTimer waitTimer;
};
