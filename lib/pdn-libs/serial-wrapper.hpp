//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once

#include <string>

using namespace std;

class HWSerialWrapper {
    public:
    virtual void begin() = 0;
    virtual ~HWSerialWrapper() {};
    virtual int availableForWrite() = 0;
    virtual int available() = 0;
    virtual int peek() = 0;
    virtual int read() = 0;
    virtual string readStringUntil(char terminator) = 0;
    virtual void print(char msg) = 0;
    virtual void println(char* msg) = 0;
    virtual void println(string msg) = 0;
    virtual void flush() = 0;
};