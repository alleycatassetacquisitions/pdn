//
// Created by Elli Furedy on 10/5/2024.
//
#pragma once

class Peripherals {
public:
    Peripherals();

    ~Peripherals();

    virtual void tick() = 0;
};
