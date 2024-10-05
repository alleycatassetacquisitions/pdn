//
// Created by Elli Furedy on 10/5/2024.
//
#pragma once
#include "pdn-peripherals.hpp"
#include "device-interface/device.hpp"

class PDN : public Device {
    public:
    static PDN* GetInstance();

    int begin() override;
    void tick() override;

    Peripherals getPeripherals() override;

protected:
    void initializePins() override;

private:
    PDN();
    PDNPeripherals peripherals;
};
