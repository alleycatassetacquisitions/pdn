//
// Created by Elli Furedy on 10/12/2024.
//

#pragma once
#include "device/drivers/driver-interface.hpp"
#include <Arduino.h>

class Esp32S3HapticsDriver : public HapticsMotorDriverInterface {
public:
    Esp32S3HapticsDriver(const std::string& name, int pin) 
        : HapticsMotorDriverInterface(name), pinNumber(pin), intensity(0), active(false) {
    }

    ~Esp32S3HapticsDriver() override {
        analogWrite(pinNumber, 0);
    }

    int initialize() override {
        pinMode(pinNumber, OUTPUT);
        return 0;
    }

    void exec() override {
        // No periodic execution needed for haptics driver
    }

    bool isOn() override {
        return active;
    }

    void max() override {
        intensity = 255;
        active = true;
        analogWrite(pinNumber, 255);
    }

    void setIntensity(int value) override {
        if (value > 255) intensity = 255;
        else if (value < 0) intensity = 0;
        else intensity = value;

        analogWrite(pinNumber, intensity);
    }

    int getIntensity() override {
        return intensity;
    }

    void off() override {
        intensity = 0;
        active = false;
        analogWrite(pinNumber, 0);
    }

private:
    int pinNumber;
    int intensity;
    bool active;
};
