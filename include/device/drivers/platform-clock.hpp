#pragma once

class PlatformClock {
public:
    virtual ~PlatformClock() = default;
    virtual unsigned long milliseconds() = 0;
};