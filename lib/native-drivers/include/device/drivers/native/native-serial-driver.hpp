#pragma once

#include "device/drivers/driver-interface.hpp"
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

    int availableForWrite() override {
        // Return remaining space in output buffer
        return static_cast<int>(MAX_OUTPUT_BUFFER_SIZE - outputBuffer.size());
    }

    int available() override {
        return inputBuffer.empty() ? 0 : 1;
    }

    int peek() override {
        if (inputBuffer.empty()) return -1;
        return inputBuffer.front()[0];
    }

    int read() override {
        if (inputBuffer.empty()) return -1;
        char c = inputBuffer.front()[0];
        if (inputBuffer.front().length() == 1) {
            inputBuffer.pop();
        } else {
            inputBuffer.front() = inputBuffer.front().substr(1);
        }
        return c;
    }

    std::string readStringUntil(char terminator) override {
        if (inputBuffer.empty()) return "";
        std::string result = inputBuffer.front();
        inputBuffer.pop();
        // Remove terminator if present
        if (!result.empty() && result.back() == terminator) {
            result.pop_back();
        }
        // Note: History tracking is done in injectInput() when data arrives
        return result;
    }

    void print(char msg) override {
        outputBuffer += msg;
        trimOutputBuffer();
        if (routedPeer != nullptr && routedPeer->hasByteCallback()) {
            routePendingOutput();
        }
    }

    void println(char* msg) override {
        println(std::string(msg));
    }

    void println(const std::string& msg) override {
        outputBuffer += msg;
        outputBuffer += '\n';
        trimOutputBuffer();
        // Track sent message
        addToHistory(sentHistory, msg);
        routePendingOutput();
    }

    void flush() override {
        // In native simulation, we intentionally do NOT clear the buffer here.
        // This prevents data loss when state transitions call flushSerial() before
        // the SerialCableBroker has a chance to transfer the data.
        // The broker calls clearOutput() after successfully transferring data.
    }

    void setStringCallback(const SerialStringCallback& callback) override {
        stringCallback = callback;
    }

    /** Routes RX bytes to the byte callback in exec(); see HWSerialWrapper. */
    void setByteCallback(const SerialByteCallback& callback) override {
        byteCallback = callback;
    }

    /**
     * Connects this jack's TX output directly to `peer`'s RX queue, or passes
     * nullptr to disconnect the simulated cable.
     */
    void routeTo(HWSerialWrapper* peer) override {
        routedPeer = static_cast<NativeSerialDriver*>(peer);
    }

    /**
     * True when a byte callback is installed (HELLO/binary mode), so the cable
     * broker delivers raw bytes rather than re-framing STRING_START messages.
     */
    bool hasByteCallback() const { return static_cast<bool>(byteCallback); }

    /**
     * Test/broker helper: buffers raw bytes for exec() to drain to the byte
     * callback. The binary-framing analogue of injectInput().
     */
    void injectBytes(const std::vector<uint8_t>& bytes) {
        if (bytes.empty()) return;
        if (byteCallback && !inputBuffer.empty()) {
            inputBuffer.back().append(bytes.begin(), bytes.end());
            if (inputBuffer.back().size() > MAX_OUTPUT_BUFFER_SIZE) {
                inputBuffer.back().erase(
                    0, inputBuffer.back().size() - MAX_OUTPUT_BUFFER_SIZE);
            }
            return;
        }
        while (inputBuffer.size() >= MAX_INPUT_QUEUE_SIZE) {
            inputBuffer.pop();
        }
        inputBuffer.push(std::string(bytes.begin(), bytes.end()));
    }

    // Test helper methods
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
        
        // Track the clean message in history for UI display
        addToHistory(receivedHistory, cleanMsg);

        // Invoke callback with clean message
        if (stringCallback) {
            stringCallback(cleanMsg);
        }
    }

    std::string getOutput() const {
        return outputBuffer;
    }

    void clearOutput() {
        outputBuffer.clear();
    }

    /** Returns the sent message history for CLI diagnostics. */
    const std::deque<std::string>& getSentHistory() const { return sentHistory; }
    /** Returns the received message history for CLI diagnostics. */
    const std::deque<std::string>& getReceivedHistory() const { return receivedHistory; }

    /** Returns the number of queued RX entries. */
    size_t getInputQueueSize() const { return inputBuffer.size(); }
    /** Returns the number of buffered TX bytes. */
    size_t getOutputBufferSize() const { return outputBuffer.size(); }

private:
    std::queue<std::string> inputBuffer;
    std::string outputBuffer;
    SerialStringCallback stringCallback;
    SerialByteCallback byteCallback;
    NativeSerialDriver* routedPeer = nullptr;

    // Message history (most recent at back)
    std::deque<std::string> sentHistory;
    std::deque<std::string> receivedHistory;
    static const size_t MAX_HISTORY = 5;

    /**
     * Pushes buffered TX bytes directly into `routedPeer`'s RX queue when a
     * cable is connected via `routeTo`.
     */
    void routePendingOutput() {
        if (routedPeer == nullptr || outputBuffer.empty()) {
            return;
        }
        if (routedPeer->hasByteCallback()) {
            routedPeer->injectBytes(
                std::vector<uint8_t>(outputBuffer.begin(), outputBuffer.end()));
            outputBuffer.clear();
            return;
        }
        size_t pos = 0;
        while ((pos = outputBuffer.find('\n')) != std::string::npos) {
            std::string line = outputBuffer.substr(0, pos);
            if (!line.empty()) {
                routedPeer->injectInput(line + "\r");
            }
            outputBuffer.erase(0, pos + 1);
        }
    }

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
        if (outputBuffer.size() > MAX_OUTPUT_BUFFER_SIZE) {
            // Keep only the newest data (from the end)
            outputBuffer = outputBuffer.substr(outputBuffer.size() - MAX_OUTPUT_BUFFER_SIZE);
        }
    }
};
