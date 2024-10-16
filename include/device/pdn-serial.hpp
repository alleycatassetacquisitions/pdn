//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once
#include "serial-wrapper.hpp"
#include <string>

using namespace std;

class PDNSerialOut : public HWSerialWrapper {
    public:
    PDNSerialOut();
    ~PDNSerialOut() override {}

    void begin() override;

    int availableForWrite() override;
    int available() override;
    int peek() override;
    int read() override;
    string readStringUntil(char terminator) override;
    void print(char msg) override;
    void println(char* msg) override;
    void println(string msg) override;
};

class PDNSerialIn : public HWSerialWrapper {
public:
    PDNSerialIn();
    ~PDNSerialIn() override {}

    void begin() override;

    int availableForWrite() override;
    int available() override;
    int peek() override;
    int read() override;
    string readStringUntil(char terminator) override;
    void print(char msg) override;
    void println(char* msg) override;
    void println(string msg) override;
};