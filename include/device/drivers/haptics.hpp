#pragma once

class Haptics {
public:
    virtual ~Haptics() = default;

    virtual bool isOn() = 0;

    virtual void max() = 0;

    virtual void setIntensity(int intensity) = 0;

    virtual int getIntensity() = 0;

    virtual void off() = 0;
};
