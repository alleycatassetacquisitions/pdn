#include <Arduino.h>

#include "simple-timer.hpp"

void SimpleTimer::updateTime()
{
    now = millis();
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
