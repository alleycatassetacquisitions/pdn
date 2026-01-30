//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once

#include <string>
#include <functional>

using SerialStringCallback = std::function<void(std::string)>;

enum class SerialIdentifier {
    OUTPUT_JACK = 0,
    INPUT_JACK = 1
};

class HWSerialWrapper {
    public:
    virtual ~HWSerialWrapper() = default;
    virtual int availableForWrite() = 0;
    virtual int available() = 0;
    virtual int peek() = 0;
    virtual int read() = 0;
    virtual std::string readStringUntil(char terminator) = 0;
    virtual void print(char msg) = 0;
    virtual void println(char* msg) = 0;
    virtual void println(const std::string& msg) = 0;
    virtual void flush() = 0;
    virtual void setStringCallback(const SerialStringCallback& callback) = 0;
};