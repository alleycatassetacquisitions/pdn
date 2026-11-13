#pragma once

#include "device/drivers/driver-interface.hpp"

class NativeHapticsDriver : public HapticsMotorDriverInterface {
public:
    /** A motor that records its intensity and on/off state and drives nothing. */
    explicit NativeHapticsDriver(const std::string& name)
        : HapticsMotorDriverInterface(name)
        , intensity(0)
        , active(false) {}

    ~NativeHapticsDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // No periodic execution needed for native haptics driver
    }

    bool isOn() override {
        return active;
    }

    void max() override {
        intensity = 255;
        active = true;
    }

    void setIntensity(int value) override {
        intensity = value;
    }

    int getIntensity() override {
        return intensity;
    }

    void off() override {
        intensity = 0;
        active = false;
    }

private:
    int intensity;
    bool active;
};