//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <functional>

using SerialBytesCallback = std::function<void(const uint8_t*, size_t)>;

enum class SerialIdentifier {
    OUTPUT_JACK = 0,
    INPUT_JACK = 1
};

class HWSerialWrapper {
    public:
    virtual ~HWSerialWrapper() = default;
    virtual int availableForWrite() = 0;
    virtual int available() = 0;
    virtual void flush() = 0;
    virtual void setBytesCallback(const SerialBytesCallback& callback) = 0;
    virtual void writeBytes(const uint8_t* data, size_t len) = 0;
};
