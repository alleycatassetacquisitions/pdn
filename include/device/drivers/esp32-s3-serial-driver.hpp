//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once

#include "driver-interface.hpp"
#include <Arduino.h>
#include "device-constants.hpp"
#include <HardwareSerial.h>
#include <string>

class Esp32s3SerialOut : public SerialDriverInterface {
    public:
    Esp32s3SerialOut(std::string name) : SerialDriverInterface(name) {};
    ~Esp32s3SerialOut() override {
        stringCallback = nullptr;
    }

    int initialize() override {
        
        gpio_reset_pin(GPIO_NUM_38);
        gpio_reset_pin(GPIO_NUM_39);

        gpio_pad_select_gpio(GPIO_NUM_38);
        gpio_pad_select_gpio(GPIO_NUM_39);
        
        pinMode(TXt, OUTPUT);
        pinMode(TXr, INPUT);

        Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);
        return 0;
    };

    void exec() override {
        char incomingChar = Serial1.read();
        if (incomingChar == STRING_START) {
            std::string receivedString = std::string(Serial1.readStringUntil(STRING_TERM).c_str());
            if (stringCallback) {
                stringCallback(receivedString);
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

    void println(std::string msg) override {
        Serial1.println(msg.c_str());
    }

    void flush() override {
        Serial1.flush();
    }

    void setStringCallback(SerialStringCallback callback) override {
        stringCallback = callback;
    }

    private:
    SerialStringCallback stringCallback;
};

class Esp32s3SerialIn : public SerialDriverInterface {
public:
    Esp32s3SerialIn(std::string name) : SerialDriverInterface(name) {};
    ~Esp32s3SerialIn() override {
        stringCallback = nullptr;
    }

    int initialize() override {
        gpio_reset_pin(GPIO_NUM_40);
        gpio_reset_pin(GPIO_NUM_41);

        gpio_pad_select_gpio(GPIO_NUM_40);
        gpio_pad_select_gpio(GPIO_NUM_41);

        pinMode(RXt, OUTPUT);
        pinMode(RXr, INPUT);

        Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);
        return 0;
    };

    void exec() override {
        char incomingChar = Serial2.read();
        if (incomingChar == STRING_START) {
            std::string receivedString = std::string(Serial2.readStringUntil(STRING_TERM).c_str());
            if (stringCallback) {
                stringCallback(receivedString);
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

    void println(std::string msg) override {
        Serial2.println(msg.c_str());
    }

    void flush() override {
        Serial2.flush();
    }

    void setStringCallback(SerialStringCallback callback) override {
        stringCallback = callback;
    }

    private:
    SerialStringCallback stringCallback;
};