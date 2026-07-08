//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"
#include <Arduino.h>
#include <esp_rom_gpio.h>
#include "protocol-constants.hpp"
#include <HardwareSerial.h>
#include <string>

class Esp32s3SerialOut : public SerialDriverInterface {
public:
    explicit Esp32s3SerialOut(const std::string& name, uint8_t txPin, uint8_t rxPin)
        : SerialDriverInterface(name), txPin(txPin), rxPin(rxPin) {}

    ~Esp32s3SerialOut() override {
        stringCallback = nullptr;
    }

    int initialize() override {
        gpio_reset_pin(static_cast<gpio_num_t>(txPin));
        gpio_reset_pin(static_cast<gpio_num_t>(rxPin));

        esp_rom_gpio_pad_select_gpio(static_cast<gpio_num_t>(txPin));
        esp_rom_gpio_pad_select_gpio(static_cast<gpio_num_t>(rxPin));

        pinMode(txPin, OUTPUT);
        pinMode(rxPin, INPUT);

        Serial1.begin(BAUDRATE, SERIAL_8N1, rxPin, txPin, true);
        Serial1.setTimeout(100);  // 100ms timeout for readStringUntil
        return 0;
    };

    void exec() override {
        while (Serial1.available() > 0) {
            char incomingChar = Serial1.read();
            // Byte mode routes each RX byte to the byte callback (HELLO
            // framing) and skips string assembly; the two are mutually
            // exclusive per jack.
            if (byteCallback) {
                byteCallback(static_cast<uint8_t>(incomingChar));
                continue;
            }
            if (incomingChar == STRING_START) {
                std::string receivedString = std::string(Serial1.readStringUntil(STRING_TERM).c_str());
                LOG_D("SERIAL1", "Received: '%s' (len=%d)", receivedString.c_str(), receivedString.length());
                if (stringCallback) {
                    stringCallback(receivedString);
                }
            }
        }
    }

    int availableForWrite() override {
        return Serial1.availableForWrite();
    }

    int available() override {
        return Serial1.available();
    }

    int peek() override {
        return Serial1.peek();
    }

    int read() override {
        return Serial1.read();
    }

    std::string readStringUntil(char terminator) override {
        return std::string(Serial1.readStringUntil(terminator).c_str());
    }

    void print(char msg) override {
        Serial1.print(msg);
    }

    void println(char* msg) override {
        Serial1.println(msg);
    }

    void println(const std::string& msg) override {
        Serial1.println(msg.c_str());
    }

    void flush() override {
        Serial1.flush();
    }

    void setStringCallback(const SerialStringCallback& callback) override {
        stringCallback = callback;
    }

    /// Routes RX bytes to the byte callback in exec(); see HWSerialWrapper.
    void setByteCallback(const SerialByteCallback& callback) override {
        byteCallback = callback;
    }

private:
    SerialStringCallback stringCallback;
    SerialByteCallback byteCallback;
    uint8_t txPin;
    uint8_t rxPin;
};

class Esp32s3SerialIn : public SerialDriverInterface {
public:
    explicit Esp32s3SerialIn(const std::string& name, uint8_t txPin, uint8_t rxPin)
        : SerialDriverInterface(name), txPin(txPin), rxPin(rxPin) {}

    ~Esp32s3SerialIn() override {
        stringCallback = nullptr;
    }

    int initialize() override {
        gpio_reset_pin(static_cast<gpio_num_t>(txPin));
        gpio_reset_pin(static_cast<gpio_num_t>(rxPin));

        esp_rom_gpio_pad_select_gpio(static_cast<gpio_num_t>(txPin));
        esp_rom_gpio_pad_select_gpio(static_cast<gpio_num_t>(rxPin));

        pinMode(txPin, OUTPUT);
        pinMode(rxPin, INPUT);

        Serial2.begin(BAUDRATE, SERIAL_8N1, rxPin, txPin, true);
        Serial2.setTimeout(100);  // 100ms timeout for readStringUntil
        return 0;
    };

    void exec() override {
        while (Serial2.available() > 0) {
            char incomingChar = Serial2.read();
            // Byte mode routes each RX byte to the byte callback (HELLO
            // framing) and skips string assembly; the two are mutually
            // exclusive per jack.
            if (byteCallback) {
                byteCallback(static_cast<uint8_t>(incomingChar));
                continue;
            }
            if (incomingChar == STRING_START) {
                std::string receivedString = std::string(Serial2.readStringUntil(STRING_TERM).c_str());
                LOG_D("SERIAL2", "Received: '%s' (len=%d)", receivedString.c_str(), receivedString.length());
                if (stringCallback) {
                    stringCallback(receivedString);
                }
            }
        }
    }

    int availableForWrite() override {
        return Serial2.availableForWrite();
    }

    int available() override {
        return Serial2.available();
    }

    int peek() override {
        return Serial2.peek();
    }

    int read() override {
        return Serial2.read();
    }

    std::string readStringUntil(char terminator) override {
        return std::string(Serial2.readStringUntil(terminator).c_str());
    }

    void print(char msg) override {
        Serial2.print(msg);
    }

    void println(char* msg) override {
        Serial2.println(msg);
    }

    void println(const std::string& msg) override {
        Serial2.println(msg.c_str());
    }

    void flush() override {
        Serial2.flush();
    }

    void setStringCallback(const SerialStringCallback& callback) override {
        stringCallback = callback;
    }

    /// Routes RX bytes to the byte callback in exec(); see HWSerialWrapper.
    void setByteCallback(const SerialByteCallback& callback) override {
        byteCallback = callback;
    }

private:
    SerialStringCallback stringCallback;
    SerialByteCallback byteCallback;
    uint8_t txPin;
    uint8_t rxPin;
};

// Secondary input jack — uses Serial1 (same peripheral as SerialOut, only one active at a time).
// FDN devices have two input jacks and no output jack, so Serial1 is available for a second input.
class Esp32s3SerialInSecondary : public SerialDriverInterface {
public:
    explicit Esp32s3SerialInSecondary(const std::string& name, uint8_t txPin, uint8_t rxPin)
        : SerialDriverInterface(name), txPin(txPin), rxPin(rxPin) {}

    ~Esp32s3SerialInSecondary() override {
        stringCallback = nullptr;
    }

    int initialize() override {
        gpio_reset_pin(static_cast<gpio_num_t>(txPin));
        gpio_reset_pin(static_cast<gpio_num_t>(rxPin));

        esp_rom_gpio_pad_select_gpio(static_cast<gpio_num_t>(txPin));
        esp_rom_gpio_pad_select_gpio(static_cast<gpio_num_t>(rxPin));

        pinMode(txPin, OUTPUT);
        pinMode(rxPin, INPUT);

        Serial1.begin(BAUDRATE, SERIAL_8N1, rxPin, txPin, true);
        Serial1.setTimeout(100);
        return 0;
    }

    void exec() override {
        while (Serial1.available() > 0) {
            char incomingChar = Serial1.read();
            // Byte mode routes each RX byte to the byte callback (HELLO
            // framing) and skips string assembly; the two are mutually
            // exclusive per jack.
            if (byteCallback) {
                byteCallback(static_cast<uint8_t>(incomingChar));
                continue;
            }
            if (incomingChar == STRING_START) {
                std::string receivedString = std::string(Serial1.readStringUntil(STRING_TERM).c_str());
                LOG_D("SERIAL1_SEC", "Received: '%s' (len=%d)", receivedString.c_str(), receivedString.length());
                if (stringCallback) {
                    stringCallback(receivedString);
                }
            }
        }
    }

    int availableForWrite() override { return Serial1.availableForWrite(); }
    int available() override { return Serial1.available(); }
    int peek() override { return Serial1.peek(); }
    int read() override { return Serial1.read(); }

    std::string readStringUntil(char terminator) override {
        return std::string(Serial1.readStringUntil(terminator).c_str());
    }

    void print(char msg) override { Serial1.print(msg); }
    void println(char* msg) override { Serial1.println(msg); }
    void println(const std::string& msg) override { Serial1.println(msg.c_str()); }
    void flush() override { Serial1.flush(); }

    void setStringCallback(const SerialStringCallback& callback) override {
        stringCallback = callback;
    }

    /// Routes RX bytes to the byte callback in exec(); see HWSerialWrapper.
    void setByteCallback(const SerialByteCallback& callback) override {
        byteCallback = callback;
    }

private:
    SerialStringCallback stringCallback;
    SerialByteCallback byteCallback;
    uint8_t txPin;
    uint8_t rxPin;
};