//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"
#include <Arduino.h>
#include <esp_rom_gpio.h>
#include <driver/gpio.h>
#include "protocol-constants.hpp"
#include <HardwareSerial.h>
#include <string>

// One physical jack's UART. The two jacks differ only in which hardware UART
// they own and the RX pull direction (a per-jack electrical fact; see the
// construction sites in main.cpp), so both are instances of this one class.
class Esp32s3SerialJack : public SerialDriverInterface {
public:
    // RX idle bias for a marginal TRS contact. With invert=true the line
    // idles LOW, so a jack receiving on the cable TIP (worst contact) needs
    // PULLDOWN: a pullup would float a marginal contact HIGH and false-trip
    // the silent-link. A jack whose contact is solid uses PULLUP so a marginal
    // contact idles HIGH, matching the remote's active-LOW idle. Data pulses
    // overpower either weak pull, so reception stays intact.
    // (Not named PULLUP/PULLDOWN: those are Arduino-core preprocessor macros.)
    enum class RxPull { Down, Up };

    explicit Esp32s3SerialJack(const std::string& name, HardwareSerial& serial,
                               uint8_t txPin, uint8_t rxPin, RxPull rxPull)
        : SerialDriverInterface(name), serial_(serial),
          txPin(txPin), rxPin(rxPin), rxPull(rxPull) {}

    ~Esp32s3SerialJack() override {
        bytesCallback = nullptr;
    }

    int initialize() override {
        gpio_reset_pin(static_cast<gpio_num_t>(txPin));
        gpio_reset_pin(static_cast<gpio_num_t>(rxPin));

        esp_rom_gpio_pad_select_gpio(static_cast<gpio_num_t>(txPin));
        esp_rom_gpio_pad_select_gpio(static_cast<gpio_num_t>(rxPin));

        pinMode(txPin, OUTPUT);
        pinMode(rxPin, INPUT);

        serial_.begin(BAUDRATE, SERIAL_8N1, rxPin, txPin, true);
        serial_.setTimeout(100);

        // Drain on the UART event task: onReceive fires a symbol-gap after the
        // burst, so a HELLO's liveness timestamp is accurate to ~1ms regardless
        // of main-loop load, letting the silent-link watchdog stay tight.
        serial_.onReceive([this]() { drainUart(); });

        if (rxPull == RxPull::Down) {
            gpio_set_pull_mode(static_cast<gpio_num_t>(rxPin), GPIO_PULLDOWN_ONLY);
        } else {
            gpio_pullup_en(static_cast<gpio_num_t>(rxPin));
        }
        return 0;
    };

    // RX is drained on the UART event task (onReceive), so the per-tick driver
    // pump has nothing to do.
    void exec() override {}

    int availableForWrite() override {
        return serial_.availableForWrite();
    }

    int available() override {
        return serial_.available();
    }

    void flush() override {
        serial_.flush();
    }

    void setBytesCallback(const SerialBytesCallback& callback) override {
        bytesCallback = callback;
    }

    void writeBytes(const uint8_t* data, size_t len) override {
        serial_.write(data, len);
    }

    private:
    SerialBytesCallback bytesCallback;
    HardwareSerial& serial_;
    uint8_t txPin;
    uint8_t rxPin;
    RxPull rxPull;

    // Runs on the UART event task; touches only event-task-owned state (the
    // parser behind bytesCallback).
    void drainUart() {
        uint8_t buf[64];
        while (serial_.available() > 0) {
            size_t n = serial_.read(buf, sizeof(buf));
            if (n == 0) break;
            if (bytesCallback) {
                bytesCallback(buf, n);
            }
        }
    }
};
