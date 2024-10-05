#pragma once

class SimpleTimer {
public:
    static void updateTime();

    unsigned long getElapsedTime();

    bool expired();

    void invalidate();

    void setTimer(unsigned long timerDelay);

private:
    static unsigned long now;
    unsigned long start = 0;
    unsigned long duration = 0;
};
