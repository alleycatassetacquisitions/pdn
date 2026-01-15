//
// Created by Elli Furedy on 10/12/2024.
//

#pragma once
#include "driver-interface.hpp"
#include <Arduino.h>

class Esp32S3HapticsDriver : public HapticsMotorDriverInterface {
    public:
    Esp32S3HapticsDriver(std::string name, int pinNumber) : HapticsMotorDriverInterface(name) {
        this->pinNumber = pinNumber;
        this->intensity = 0;
        this->active = false;
    };

    ~Esp32S3HapticsDriver() override {
        analogWrite(this->pinNumber, 0);
    };

    int initialize() override {
        pinMode(this->pinNumber, OUTPUT);
        return 0;
    };

    void exec() override {
    }

    bool isOn() override {
        return active;
    };

    void max() override {
        intensity = 255;
        active = true;
        analogWrite(this->pinNumber, 255);
    };

    void setIntensity(int intensity) override {
        if (intensity > 255) this->intensity = 255;
        else if (intensity < 0) this->intensity = 0;
        else this->intensity = intensity;

        analogWrite(this->pinNumber, intensity);
    };

    int getIntensity() override {
        return intensity;
    };

    void off() override {
        intensity = 0;
        active = false;
        analogWrite(this->pinNumber, 0);
    };
};
