//
// Created by Elli Furedy on 10/12/2024.
//

#pragma once
#include "device/drivers/driver-interface.hpp"
#include <Arduino.h>

class Esp32S3HapticsDriver : public HapticsMotorDriverInterface {
public:
    Esp32S3HapticsDriver(const std::string& name, int pinNumber) 
        : HapticsMotorDriverInterface(name), pinNumber_(pinNumber), intensity_(0), active_(false) {
    }

    ~Esp32S3HapticsDriver() override {
        analogWrite(pinNumber_, 0);
    }

    int initialize() override {
        pinMode(pinNumber_, OUTPUT);
        return 0;
    }

    void exec() override {
        // No periodic execution needed for haptics driver
    }

    bool isOn() override {
        return active_;
    }

    void max() override {
        intensity_ = 255;
        active_ = true;
        analogWrite(pinNumber_, 255);
    }

    void setIntensity(int intensity) override {
        if (intensity > 255) intensity_ = 255;
        else if (intensity < 0) intensity_ = 0;
        else intensity_ = intensity;

        analogWrite(pinNumber_, intensity_);
    }

    int getIntensity() override {
        return intensity_;
    }

    void off() override {
        intensity_ = 0;
        active_ = false;
        analogWrite(pinNumber_, 0);
    }

private:
    int pinNumber_;
    int intensity_;
    bool active_;
};
