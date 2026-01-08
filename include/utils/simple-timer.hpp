#pragma once

#include "platform-clock.hpp"

class SimpleTimer {
public:
    SimpleTimer() {
        updateTime();
    }

    static void setPlatformClock(PlatformClock* platformClock) {
        clock = platformClock;
    }

    static PlatformClock* getPlatformClock() {
        return clock;
    }

    void updateTime();

    unsigned long getElapsedTime();

    bool expired();

    void invalidate();

    bool isRunning();

    void setTimer(unsigned long timerDelay);

    unsigned long now = 0;
private:
    bool running = false;
    unsigned long start = 0;
    unsigned long duration = 0;
    static PlatformClock* clock;
};
