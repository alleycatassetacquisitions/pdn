#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstring>
#include <queue>
#include <deque>
#include <string>
#include <vector>

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
        // Byte mode drains the RX buffer straight to the byte callback (HELLO
        // framing); no string assembly. Legacy string jacks push through
        // injectInput() and leave exec() a no-op.
        if (byteCallback) {
            while (available() > 0) {
                byteCallback(static_cast<uint8_t>(read()));
            }
        }
    }

    /** Room left in the buffer that holds bytes not yet handed to a peer. */
    int availableForWrite() override {
        return static_cast<int>(MAX_OUTPUT_BUFFER_SIZE - outputBuffer.size());
    }

    /** 1 while any RX entry is queued, 0 otherwise. */
    int available() override { return inputBuffer.empty() ? 0 : 1; }

    /** Next RX byte without consuming it, or -1 when nothing is queued. */
    int peek() override {
        if (inputBuffer.empty()) return -1;
        return inputBuffer.front()[0];
    }

    /** Consumes and returns the next RX byte, or -1 when nothing is queued. */
    int read() override {
        if (inputBuffer.empty()) return -1;
        char c = inputBuffer.front()[0];
        // An empty entry goes too, or exec() would spin on it.
        if (inputBuffer.front().length() <= 1) {
            inputBuffer.pop();
        } else {
            inputBuffer.front().erase(0, 1);
        }
        return c;
    }

    /** Pops the oldest RX entry whole, minus a trailing `terminator`. */
    std::string readStringUntil(char terminator) override {
        if (inputBuffer.empty()) return "";
        std::string result = inputBuffer.front();
        inputBuffer.pop();
        // Remove terminator if present
        if (!result.empty() && result.back() == terminator) {
            result.pop_back();
        }
        return result;
    }

    /** Writes one byte. */
    void print(char msg) override { transmit(&msg, 1); }

    /** Writes `msg` and a newline. */
    void println(char* msg) override {
        transmit(msg, strlen(msg));
        transmit("\n", 1);
    }

    /** Writes `msg` and a newline. */
    void println(const std::string& msg) override {
        transmit(msg.data(), msg.size());
        transmit("\n", 1);
    }

    /**
     * Drops what the jack still holds. A cabled write is in the peer's RX queue
     * before it returns, so anything left here had no cable to leave by.
     */
    void flush() override { outputBuffer.clear(); }

    /** Installs the handler injectInput() calls with each unframed message. */
    void setStringCallback(const SerialStringCallback& callback) override {
        stringCallback = callback;
    }

    /** Routes RX bytes to the byte callback in exec(); see HWSerialWrapper. */
    void setByteCallback(const SerialByteCallback& callback) override { byteCallback = callback; }

    /**
     * Cables this jack's TX to `peer`'s RX, or uncables it with nullptr. Every
     * later write lands in the peer's RX queue as it is made, and bytes the jack
     * was still holding cross first. One direction only, so a cable is two calls.
     * The caller uncables before either driver is destroyed.
     */
    void routeTo(NativeSerialDriver* peer) {
        cablePeer = peer;
        deliverPendingOutput();
    }

    /**
     * Test helper: buffers raw bytes for exec() to drain to the byte callback.
     * The binary-framing analogue of injectInput().
     */
    void injectBytes(const std::vector<uint8_t>& bytes) {
        while (inputBuffer.size() >= MAX_INPUT_QUEUE_SIZE) {
            inputBuffer.pop();
        }
        inputBuffer.push(std::string(bytes.begin(), bytes.end()));
    }

    /** Test helper: queues one string message and hands it, unframed, to the string callback. */
    void injectInput(const std::string& input) {
        // Enforce FIFO limit on input queue
        while (inputBuffer.size() >= MAX_INPUT_QUEUE_SIZE) {
            inputBuffer.pop();  // Drop oldest
        }
        inputBuffer.push(input);

        // Clean the message by stripping framing (if present)
        // Framing: STRING_START (*) at beginning, STRING_TERM (\r) at end
        std::string cleanMsg = input;
        // Remove STRING_START prefix if present
        if (!cleanMsg.empty() && cleanMsg[0] == '*') {
            cleanMsg = cleanMsg.substr(1);
        }
        // Remove STRING_TERM suffix if present
        if (!cleanMsg.empty() && cleanMsg.back() == '\r') {
            cleanMsg.pop_back();
        }

        // Invoke callback with clean message
        if (stringCallback) {
            stringCallback(cleanMsg);
        }
    }

    /** Bytes written while no cable was there to carry them. */
    std::string getOutput() const { return outputBuffer; }

    /** Discards the bytes getOutput() would return. */
    void clearOutput() { outputBuffer.clear(); }

    /** RX entries waiting to be read. */
    size_t getInputQueueSize() const { return inputBuffer.size(); }
    /** Bytes getOutput() would return. */
    size_t getOutputBufferSize() const { return outputBuffer.size(); }

private:
    std::queue<std::string> inputBuffer;
    std::string outputBuffer;
    SerialStringCallback stringCallback;
    SerialByteCallback byteCallback;
    // Not owned. Whoever called routeTo() clears it before the peer is destroyed.
    NativeSerialDriver* cablePeer = nullptr;

    void transmit(const char* data, size_t length) {
        // An empty write would leave the peer an empty RX entry, which reads
        // back as a NUL byte.
        if (length == 0) return;
        outputBuffer.append(data, length);
        trimOutputBuffer();
        deliverPendingOutput();
    }

    void deliverPendingOutput() {
        if (cablePeer == nullptr || outputBuffer.empty()) return;
        cablePeer->receiveBytes(outputBuffer.data(), outputBuffer.size());
        outputBuffer.clear();
    }

    // Appends to the undrained RX chunk, not a new entry: a frame arrives a byte
    // at a time and the 32-entry cap would drop its head. Capped at
    // MAX_OUTPUT_BUFFER_SIZE, keeping the newest bytes.
    void receiveBytes(const char* data, size_t length) {
        if (inputBuffer.empty()) inputBuffer.emplace();
        std::string& pending = inputBuffer.back();
        pending.append(data, length);
        if (pending.size() > MAX_OUTPUT_BUFFER_SIZE) {
            pending.erase(0, pending.size() - MAX_OUTPUT_BUFFER_SIZE);
        }
    }

    /**
     * Trim output buffer to max size using FIFO - drops oldest data from front.
     */
    void trimOutputBuffer() {
        if (outputBuffer.size() > MAX_OUTPUT_BUFFER_SIZE) {
            // Keep only the newest data (from the end)
            outputBuffer = outputBuffer.substr(outputBuffer.size() - MAX_OUTPUT_BUFFER_SIZE);
        }
    }
};
