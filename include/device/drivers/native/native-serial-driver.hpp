#pragma once

#include "device/drivers/driver-interface.hpp"
#include <queue>
#include <string>

class NativeSerialDriver : public SerialDriverInterface {
public:
    NativeSerialDriver(std::string name) : SerialDriverInterface(name) {}

    ~NativeSerialDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // Process any incoming serial data
    }

    int availableForWrite() override {
        return 1024; // Always ready to write
    }

    int available() override {
        return inputBuffer_.empty() ? 0 : 1;
    }

    int peek() override {
        if (inputBuffer_.empty()) return -1;
        return inputBuffer_.front()[0];
    }

    int read() override {
        if (inputBuffer_.empty()) return -1;
        char c = inputBuffer_.front()[0];
        if (inputBuffer_.front().length() == 1) {
            inputBuffer_.pop();
        } else {
            inputBuffer_.front() = inputBuffer_.front().substr(1);
        }
        return c;
    }

    std::string readStringUntil(char terminator) override {
        if (inputBuffer_.empty()) return "";
        std::string result = inputBuffer_.front();
        inputBuffer_.pop();
        // Remove terminator if present
        if (!result.empty() && result.back() == terminator) {
            result.pop_back();
        }
        return result;
    }

    void print(char msg) override {
        outputBuffer_ += msg;
    }

    void println(char* msg) override {
        outputBuffer_ += msg;
        outputBuffer_ += '\n';
    }

    void println(const std::string& msg) override {
        outputBuffer_ += msg;
        outputBuffer_ += '\n';
    }

    void flush() override {
        // In native, could print to stdout if desired
        outputBuffer_.clear();
    }

    void setStringCallback(const SerialStringCallback& callback) override {
        stringCallback_ = callback;
    }

    // Test helper methods
    void injectInput(const std::string& input) {
        inputBuffer_.push(input);
        if (stringCallback_) {
            stringCallback_(input);
        }
    }

    std::string getOutput() const {
        return outputBuffer_;
    }

    void clearOutput() {
        outputBuffer_.clear();
    }

private:
    std::queue<std::string> inputBuffer_;
    std::string outputBuffer_;
    SerialStringCallback stringCallback_;
};
