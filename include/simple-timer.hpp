#pragma once

class SimpleTimer {
public:
    SimpleTimer() {
        updateTime();
    }

    void updateTime();

    unsigned long getElapsedTime();

    bool expired();

    void invalidate();

    void setTimer(unsigned long timerDelay);

    unsigned long now = 0;
private:
    bool running = false;
    unsigned long start = 0;
    unsigned long duration = 0;
};
