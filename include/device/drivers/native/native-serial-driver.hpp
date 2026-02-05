#pragma once

#include "device/drivers/driver-interface.hpp"
#include <queue>
#include <deque>
#include <string>

class NativeSerialDriver : public SerialDriverInterface {
public:
    // Maximum buffer sizes to prevent unbounded growth
    static constexpr size_t MAX_OUTPUT_BUFFER_SIZE = 255;
    static constexpr size_t MAX_INPUT_QUEUE_SIZE = 32;
    
    explicit NativeSerialDriver(const std::string& name) : SerialDriverInterface(name) {}

    ~NativeSerialDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // Process any incoming serial data
    }

    int availableForWrite() override {
        // Return remaining space in output buffer
        return static_cast<int>(MAX_OUTPUT_BUFFER_SIZE - outputBuffer_.size());
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
        // Track received message
        addToHistory(receivedHistory_, result);
        return result;
    }

    void print(char msg) override {
        outputBuffer_ += msg;
        trimOutputBuffer();
    }

    void println(char* msg) override {
        outputBuffer_ += msg;
        outputBuffer_ += '\n';
        trimOutputBuffer();
        // Track sent message
        addToHistory(sentHistory_, std::string(msg));
    }

    void println(const std::string& msg) override {
        outputBuffer_ += msg;
        outputBuffer_ += '\n';
        trimOutputBuffer();
        // Track sent message
        addToHistory(sentHistory_, msg);
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
        // Enforce FIFO limit on input queue
        while (inputBuffer_.size() >= MAX_INPUT_QUEUE_SIZE) {
            inputBuffer_.pop();  // Drop oldest
        }
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
    
    // Message history access for CLI display
    const std::deque<std::string>& getSentHistory() const { return sentHistory_; }
    const std::deque<std::string>& getReceivedHistory() const { return receivedHistory_; }
    
    // Get current buffer sizes for display
    size_t getInputQueueSize() const { return inputBuffer_.size(); }
    size_t getOutputBufferSize() const { return outputBuffer_.size(); }

private:
    std::queue<std::string> inputBuffer_;
    std::string outputBuffer_;
    SerialStringCallback stringCallback_;
    
    // Message history (most recent at back)
    std::deque<std::string> sentHistory_;
    std::deque<std::string> receivedHistory_;
    static const size_t MAX_HISTORY = 5;
    
    void addToHistory(std::deque<std::string>& history, const std::string& msg) {
        history.push_back(msg);
        while (history.size() > MAX_HISTORY) {
            history.pop_front();
        }
    }
    
    /**
     * Trim output buffer to max size using FIFO - drops oldest data from front.
     */
    void trimOutputBuffer() {
        if (outputBuffer_.size() > MAX_OUTPUT_BUFFER_SIZE) {
            // Keep only the newest data (from the end)
            outputBuffer_ = outputBuffer_.substr(outputBuffer_.size() - MAX_OUTPUT_BUFFER_SIZE);
        }
    }
};
