#pragma once

class PlatformClock {
public:
    virtual ~PlatformClock() {}
    virtual unsigned long milliseconds() = 0;
};