#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// A validated binary frame surfaced to downstream consumers (RDC's ingestSerial,
// bound via setBinaryFrameHandler). Payload layouts are owned by
// peer-graph-codec.hpp (kHelloPayloadLen / kBeaconPayloadLen); payload holds the
// raw bytes between the opcode and the CRC.
struct Frame {
    uint8_t opcode;
    std::vector<uint8_t> payload;
};

using BinaryFrameHandler = std::function<void(const Frame&)>;

// Parses one jack's serial byte stream into framed binary peer-graph frames
// (preamble 0xAA 0x55 + opcode + payload + CRC-16). One instance per jack.
// Binary framing because the jacks are physically unreliable (TRS contacts,
// partial insertion): a corrupted delimited string still parses as *some*
// string, while a frame failing CRC is dropped and the preamble resyncs.
// feed() runs on the UART event task; requestReset() is callable from the main
// loop and is honored at the start of the next feed(), so the parser's
// std::vector state is only ever mutated by the feeding thread.
class SerialFrameParser {
public:
    void feed(const uint8_t* data, size_t len);

    // Request a parser reset from another thread. The actual clear happens at
    // the start of the next feed(), keeping parser state single-owner.
    void requestReset() { resetRequested_.store(true); }

    void setBinaryFrameHandler(BinaryFrameHandler cb) { binaryFrameHandler_ = std::move(cb); }

private:
    enum class ParseState {
        ScanForSync,
        GotAA,
        ReadingOpcode,
        ReadingPayload,
        ReadingCrc,
    };

    struct ParserState {
        ParseState state = ParseState::ScanForSync;
        uint8_t opcode = 0;
        std::vector<uint8_t> payloadBuf;
        size_t expectedPayloadLen = 0;
        uint8_t crcBytes[2] = {0, 0};
        size_t crcBytesRead = 0;
    };

    ParserState parser_;

    // Timestamp of the last arriving byte; checkTimeout() uses it to clear a
    // stalled mid-frame parse.
    unsigned long lastByteMs_ = 0;

    std::atomic<bool> resetRequested_{false};

    BinaryFrameHandler binaryFrameHandler_;

    // Wall-clock window after which a mid-frame parser resets to scan-for-sync.
    static constexpr unsigned long kParserTimeoutMs = 50;

    void resetParser();
    void checkTimeout();
    static size_t opcodePayloadLen(uint8_t opcode);
    static unsigned long nowMs();
};
