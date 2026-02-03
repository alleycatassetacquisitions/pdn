#pragma once

#include "device/drivers/driver-interface.hpp"

class NativeHapticsDriver : public HapticsMotorDriverInterface {
public:
    NativeHapticsDriver(const std::string& name, int pin) 
        : HapticsMotorDriverInterface(name), pinNumber(pin), intensity(0), active(false) {
    }

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
    int pinNumber;
    int intensity;
    bool active;
};