#pragma once

#include "device/drivers/driver-interface.hpp"

class NativeHapticsDriver : public HapticsMotorDriverInterface {
public:
    NativeHapticsDriver(const std::string& name, int pinNumber) 
        : HapticsMotorDriverInterface(name), pinNumber_(pinNumber), intensity_(0), active_(false) {
    }

    ~NativeHapticsDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // No periodic execution needed for native haptics driver
    }
    
    bool isOn() override {
        return active_;
    }

    void max() override {
        intensity_ = 255;
        active_ = true;
    }

    void setIntensity(int intensity) override {
        intensity_ = intensity;
    }

    int getIntensity() override {
        return intensity_;
    }

    void off() override {
        intensity_ = 0;
        active_ = false;
    }

private:
    int pinNumber_;
    int intensity_;
    bool active_;
};