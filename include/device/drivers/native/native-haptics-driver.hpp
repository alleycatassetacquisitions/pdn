#pragma once

#include "device/drivers/driver-interface.hpp"

class NativeHapticsDriver : public HapticsMotorDriverInterface {
    public:
    NativeHapticsDriver(std::string name, int pinNumber) : HapticsMotorDriverInterface(name) {
        this->pinNumber = pinNumber;
        this->intensity = 0;
        this->active = false;
    };

    ~NativeHapticsDriver() override {
    };

    int initialize() override {
        return 0;
    };

    void exec() override {
    };
    
    bool isOn() override {
        return active;
    }

    void max() override {
        intensity = 255;
        active = true;
    }

    void setIntensity(int intensity) override {
        this->intensity = intensity;
    }

    int getIntensity() override {
        return intensity;
    }

    void off() override {
        intensity = 0;
        active = false;
    }
};