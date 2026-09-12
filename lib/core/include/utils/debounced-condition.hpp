#pragma once

#include "utils/simple-timer.hpp"

// Debounce a bool: returns true once `condition` has been continuously true for
// `windowMs`. Becoming false resets the window. Level-triggered, not edge — a
// caller that stops sampling leaves the window running unwatched, and the next
// ask reads as held however long the condition was actually false in between.
// Cable state flickers for a tick or two on real hardware; this is the grace
// window before anyone acts on it.
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
