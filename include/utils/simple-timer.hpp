#pragma once

#include "platform-clock.hpp"
#include "logger.hpp"

class SimpleTimer {
public:
    SimpleTimer() = default;

    static void setPlatformClock(PlatformClock* platformClock) {
        clock = platformClock;
    }

    static PlatformClock* getPlatformClock() {
        return clock;
    }

    void updateTime(){
        if (clock == nullptr) {
            LOG_E("SimpleTimer", "updateTime called with Platform clock not set");
            return;
        }
        now = clock->milliseconds();
    }
    
    unsigned long getElapsedTime()
    {
        updateTime();
        return now - start;
    }
    
    bool expired()
    {
        if(running) {
            return duration < getElapsedTime();
        }
    
        return false;
    }
    
    bool isRunning() {
        return running;
    }
    
    void invalidate()
    {
        duration = 0;
        running = false;
    }
    
    void setTimer(unsigned long timerDelay)
    {
        running = true;
        duration = timerDelay;
        updateTime();
        start = now;
    }

    unsigned long now = 0;

private:

    bool running = false;
    unsigned long start = 0;
    unsigned long duration = 0;
    static PlatformClock* clock;
};
