//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once

#include <cstdint>
#include <string>
#include <functional>

using SerialStringCallback = std::function<void(std::string)>;
using SerialByteCallback = std::function<void(uint8_t)>;

enum class SerialIdentifier {
    OUTPUT_JACK = 0,
    INPUT_JACK = 1,
    INPUT_JACK_SECONDARY = 2,
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
    /// When a byte callback is set, exec() routes each RX byte to it and does NOT
    /// assemble strings on this jack; a null byte callback leaves the legacy
    /// string path untouched. HELLO framing and delimited strings share one RX
    /// pump and are mutually exclusive per jack.
    virtual void setByteCallback(const SerialByteCallback& callback) = 0;
};