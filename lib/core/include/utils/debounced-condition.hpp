#pragma once

#include "utils/simple-timer.hpp"

// True once `condition` has been continuously true for `windowMs`. Sampled by
// level: stop sampling and the window keeps running, so the next ask reads held.
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
