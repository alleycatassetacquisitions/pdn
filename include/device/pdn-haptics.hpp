//
// Created by Elli Furedy on 10/12/2024.
//

#pragma once
#include "haptics.hpp"

class PDNHaptics : public Haptics {
    public:
    explicit PDNHaptics(int pinNumber);

    bool isOn() override;

    void max() override;

    void setIntensity(int intensity) override;

    int getIntensity() override;

    void off() override;
};
