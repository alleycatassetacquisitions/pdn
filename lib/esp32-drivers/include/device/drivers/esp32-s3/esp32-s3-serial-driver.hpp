//
// Created by Elli Furedy on 10/9/2024.
//

#pragma once

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"
#include "protocol-constants.hpp"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <hal/uart_ll.h>
#include <string>

/**
 * One serial jack on a UART peripheral.
 *
 * The three jacks below differ only in which UART they sit on, so the behaviour
 * lives here once. The methods reproduce what Arduino's HardwareSerial did,
 * with one deliberate exception noted at the clock source in initialize(),
 * because SerialManager reads this driver through Stream-shaped calls: it peeks
 * for STRING_START, then takes the frame with readStringUntil.
 */
class Esp32s3SerialPort : public SerialDriverInterface {
public:
    /**
     * Binds this jack to a UART. The port is fixed per jack by the subclasses below.
     */
    Esp32s3SerialPort(const std::string& name, uart_port_t port, const char* logTag,
                      uint8_t txPin, uint8_t rxPin)
        : SerialDriverInterface(name)
        , port(port)
        , logTag(logTag)
        , txPin(txPin)
        , rxPin(rxPin) {}

    /**
     * Drops the callback and releases the UART so the port can be reopened.
     */
    ~Esp32s3SerialPort() override {
        stringCallback = nullptr;
        if (uart_is_driver_installed(port)) {
            uart_driver_delete(port);
        }
    }

    /**
     * Opens the UART with the jack's signalling: 19200 8N1, both lines inverted.
     */
    int initialize() override {
        gpio_reset_pin(static_cast<gpio_num_t>(txPin));
        gpio_reset_pin(static_cast<gpio_num_t>(rxPin));

        uart_config_t config = {};
        config.baud_rate = BAUDRATE;
        config.data_bits = UART_DATA_8_BITS;
        config.parity = UART_PARITY_DISABLE;
        config.stop_bits = UART_STOP_BITS_1;
        config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        config.source_clk = UART_SCLK_DEFAULT;

        // 256-byte receive ring and no transmit ring, which is what
        // HardwareSerial defaulted to, so writes block the same way they did.
        if (uart_driver_install(port, RX_BUFFER_BYTES, 0, 0, nullptr, 0) != ESP_OK) {
            LOG_E(logTag, "uart_driver_install failed");
            return -1;
        }
        if (uart_param_config(port, &config) != ESP_OK) {
            LOG_E(logTag, "uart_param_config failed");
            return -1;
        }
        if (uart_set_pin(port, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
            LOG_E(logTag, "uart_set_pin failed");
            return -1;
        }
        // Serial1.begin(..., true) passed invert=true. The jack drives an
        // inverted line, so dropping this reads nothing but noise.
        if (uart_set_line_inverse(port, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV) != ESP_OK) {
            LOG_E(logTag, "uart_set_line_inverse failed");
            return -1;
        }
        return 0;
    }

    /**
     * Drains whole frames into the string callback, one per STRING_START seen.
     */
    void exec() override {
        while (available() > 0) {
            int incomingChar = read();
            if (incomingChar == STRING_START) {
                std::string receivedString = readStringUntil(STRING_TERM);
                LOG_D(logTag, "Received: '%s' (len=%d)", receivedString.c_str(), receivedString.length());
                if (stringCallback) {
                    stringCallback(receivedString);
                }
            }
        }
    }

    /**
     * Free space in the transmit FIFO, which is what HardwareSerial reported
     * with no transmit ring buffer configured.
     */
    int availableForWrite() override {
        return static_cast<int>(uart_ll_get_txfifo_len(UART_LL_GET_HW(port)));
    }

    /**
     * Buffered received bytes, including one held back by peek().
     */
    int available() override {
        size_t buffered = 0;
        uart_get_buffered_data_len(port, &buffered);
        return static_cast<int>(buffered) + (peeked >= 0 ? 1 : 0);
    }

    /**
     * Next byte without consuming it. The UART driver has no peek of its own,
     * so the byte is read and held here until read() takes it.
     */
    int peek() override {
        if (peeked < 0) {
            peeked = readByte(0);
        }
        return peeked;
    }

    int read() override {
        if (peeked >= 0) {
            int value = peeked;
            peeked = -1;
            return value;
        }
        return readByte(0);
    }

    /**
     * Reads up to the terminator, which is consumed and left out of the result.
     * The timeout is per byte, as Stream::timedRead applied it, so a frame
     * arriving slowly is still assembled whole.
     */
    std::string readStringUntil(char terminator) override {
        std::string result;
        int c = readWithTimeout();
        while (c >= 0 && static_cast<char>(c) != terminator) {
            result += static_cast<char>(c);
            c = readWithTimeout();
        }
        return result;
    }

    void print(char msg) override {
        uart_write_bytes(port, &msg, 1);
    }

    void println(char* msg) override {
        println(std::string(msg));
    }

    void println(const std::string& msg) override {
        uart_write_bytes(port, msg.data(), msg.size());
        uart_write_bytes(port, "\r\n", 2);
    }

    void flush() override {
        uart_wait_tx_done(port, portMAX_DELAY);
    }

    void setStringCallback(const SerialStringCallback& callback) override {
        stringCallback = callback;
    }

private:
    int readByte(uint32_t timeoutMs) {
        uint8_t byte = 0;
        int read = uart_read_bytes(port, &byte, 1, pdMS_TO_TICKS(timeoutMs));
        return read == 1 ? byte : -1;
    }

    int readWithTimeout() {
        if (peeked >= 0) {
            int value = peeked;
            peeked = -1;
            return value;
        }
        return readByte(READ_TIMEOUT_MS);
    }

    static constexpr int RX_BUFFER_BYTES = 256;
    static constexpr uint32_t READ_TIMEOUT_MS = 100;

    uart_port_t port;
    const char* logTag;
    SerialStringCallback stringCallback;
    int peeked = -1;
    uint8_t txPin;
    uint8_t rxPin;
};

class Esp32s3SerialOut : public Esp32s3SerialPort {
public:
    /**
     * The output jack, on UART1.
     */
    explicit Esp32s3SerialOut(const std::string& name, uint8_t txPin, uint8_t rxPin)
        : Esp32s3SerialPort(name, UART_NUM_1, "SERIAL1", txPin, rxPin) {}
};

class Esp32s3SerialIn : public Esp32s3SerialPort {
public:
    /**
     * The input jack, on UART2.
     */
    explicit Esp32s3SerialIn(const std::string& name, uint8_t txPin, uint8_t rxPin)
        : Esp32s3SerialPort(name, UART_NUM_2, "SERIAL2", txPin, rxPin) {}
};

// Secondary input jack — uses UART1, the same peripheral as SerialOut, only one
// active at a time. FDN devices have two input jacks and no output jack, so
// UART1 is available for a second input.
class Esp32s3SerialInSecondary : public Esp32s3SerialPort {
public:
    /**
     * The FDN's second input jack, sharing UART1 with the output jack.
     */
    explicit Esp32s3SerialInSecondary(const std::string& name, uint8_t txPin, uint8_t rxPin)
        : Esp32s3SerialPort(name, UART_NUM_1, "SERIAL1_SEC", txPin, rxPin) {}
};
