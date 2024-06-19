#include <Arduino.h>

#include "../include/simple-timer.hpp"
#include "simple-timer.hpp"

unsigned long SimpleTimer::now = 0;

void SimpleTimer::updateTime()
{
    now = millis();
}

unsigned long SimpleTimer::getElapsedTime()
{
    return now - start;
}

bool SimpleTimer::expired()
{
    return duration < getElapsedTime();
}

void SimpleTimer::invalidate()
{
    duration = 0;
}

void SimpleTimer::setTimer(unsigned long timerDelay)
{
    duration = timerDelay;
    start = millis();
}
