#include "utils/simple-timer.hpp"

PlatformClock* SimpleTimer::clock = nullptr;

void SimpleTimer::updateTime()
{
    now = clock->milliseconds();
}

unsigned long SimpleTimer::getElapsedTime()
{
    updateTime();
    return now - start;
}

bool SimpleTimer::expired()
{
    if(running) {
        return duration < getElapsedTime();
    }

    return false;
}

bool SimpleTimer::isRunning() {
    return running;
}

void SimpleTimer::invalidate()
{
    duration = 0;
    running = false;
}

void SimpleTimer::setTimer(unsigned long timerDelay)
{
    running = true;
    duration = timerDelay;
    updateTime();
    start = now;
}
