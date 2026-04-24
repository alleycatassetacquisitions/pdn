#pragma once

#include "utils/simple-timer.hpp"

// Edge-debounce a bool: returns true once `condition` has been continuously
// true for `windowMs`. Becoming false resets the window. Used by ShootoutProposal/
// ShootoutBracketReveal for loop-break detection and by ConnectState for
// disconnect detection — both patterns flicker for a tick or two in real
// hardware and need a grace window before acting.
class DebouncedCondition {
public:
    bool heldFor(bool condition, unsigned long windowMs) {
        if (!condition) {
            timer_.invalidate();
            return false;
        }
        if (!timer_.isRunning()) {
            timer_.setTimer(windowMs);
            return false;
        }
        return timer_.expired();
    }

    void reset() { timer_.invalidate(); }

private:
    SimpleTimer timer_;
};
