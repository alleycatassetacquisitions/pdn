#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstdint>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

class NativeSerialDriver : public SerialDriverInterface {
public:
    // Maximum buffer sizes to prevent unbounded growth
    static constexpr size_t MAX_OUTPUT_BUFFER_SIZE = 255;

    explicit NativeSerialDriver(const std::string& name) : SerialDriverInterface(name) {}

    ~NativeSerialDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // With fragmentation on, deliver only 1-3 bytes per tick so byte-stream
        // parsers see the partial reads they face on real UART hardware.
        if (pendingBytes_.empty()) {
            return;
        }
        size_t chunkSize = pendingBytes_.size();
        if (fragmentDeliveries_) {
            chunkSize = static_cast<size_t>((fragmentCounter_ % 3) + 1);
            fragmentCounter_++;
            if (chunkSize > pendingBytes_.size()) {
                chunkSize = pendingBytes_.size();
            }
        }
        std::vector<uint8_t> chunk(pendingBytes_.begin(),
                                   pendingBytes_.begin() + chunkSize);
        pendingBytes_.erase(pendingBytes_.begin(),
                            pendingBytes_.begin() + chunkSize);
        if (bytesCallback_) {
            bytesCallback_(chunk.data(), chunk.size());
        }
    }

    int availableForWrite() override {
        return static_cast<int>(MAX_OUTPUT_BUFFER_SIZE - outputBuffer_.size());
    }

    int available() override {
        return pendingBytes_.empty() ? 0 : 1;
    }

    void flush() override {
        // In native simulation, we intentionally do NOT clear the buffer here.
        // This prevents data loss when state transitions call flushSerial() before
        // the SerialCableBroker has a chance to transfer the data.
        // The broker calls clearOutput() after successfully transferring data.
    }

    void setBytesCallback(const SerialBytesCallback& callback) override {
        bytesCallback_ = callback;
    }

    void writeBytes(const uint8_t* data, size_t len) override {
        for (size_t i = 0; i < len; ++i) {
            outputBuffer_ += static_cast<char>(data[i]);
        }
        trimOutputBuffer();
        addToHistory(sentHistory_, describeBytes(data, len));
    }

    // Toggle artificial fragmentation of byte deliveries; used by tests to
    // exercise byte-stream parser accumulators that would otherwise never see
    // partial input on native.
    void setFragmentDeliveriesForTest(bool fragment) {
        fragmentDeliveries_ = fragment;
        fragmentCounter_ = 0;
    }

    // Enqueue raw bytes that exec() will drain to the byte callback. The
    // SerialCableBroker uses this to route one jack's emitted bytes into the
    // peer jack's RX path; tests use it to feed binary frames.
    void injectInputBytes(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            pendingBytes_.push_back(data[i]);
        }
        addToHistory(receivedHistory_, describeBytes(data, len));
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
    size_t getInputQueueSize() const { return pendingBytes_.size(); }
    size_t getOutputBufferSize() const { return outputBuffer_.size(); }

private:
    std::string outputBuffer_;
    SerialBytesCallback bytesCallback_;
    std::deque<uint8_t> pendingBytes_;
    bool fragmentDeliveries_ = false;
    size_t fragmentCounter_ = 0;

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

    // Hex-render a byte burst for the CLI's serial-traffic display panes.
    static std::string describeBytes(const uint8_t* data, size_t len) {
        static const char* hex = "0123456789ABCDEF";
        std::string s;
        s.reserve(len * 3);
        for (size_t i = 0; i < len; ++i) {
            if (i) s += ' ';
            s += hex[(data[i] >> 4) & 0x0F];
            s += hex[data[i] & 0x0F];
        }
        return s;
    }

    // FIFO trim: drop oldest data from the front, keep the newest tail.
    void trimOutputBuffer() {
        if (outputBuffer_.size() > MAX_OUTPUT_BUFFER_SIZE) {
            outputBuffer_ = outputBuffer_.substr(outputBuffer_.size() - MAX_OUTPUT_BUFFER_SIZE);
        }
    }
};
