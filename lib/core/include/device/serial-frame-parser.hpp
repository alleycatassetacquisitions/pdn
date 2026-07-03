#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Serial frame protocol constants. HELLO is the sole opcode on the wire:
// serial carries physical-connectivity discovery/liveness only, everything
// else rides ESP-NOW. The framing (preamble + opcode + payload + CRC-16)
// recreates for UART what 802.11 gives ESP-NOW for free: message boundaries
// and integrity on a raw byte stream with flaky TRS contacts.
constexpr uint8_t FRAME_PREAMBLE_0 = 0xAA;
constexpr uint8_t FRAME_PREAMBLE_1 = 0x55;
constexpr uint8_t OP_HELLO = 0x00;
// Payload bytes after the opcode, before the CRC: source mac[6] +
// deviceType[1] + headMac[6] + confirmed[1]. The packed HelloPayload struct
// that owns this layout arrives with the HELLO wire-format work; the parser
// only needs the length to frame it.
constexpr size_t HELLO_PAYLOAD_LEN = 14;

// Wraps a payload in the framed wire format: preamble, opcode, payload,
// CRC-16 over (opcode + payload). The inverse of SerialFrameParser.
std::vector<uint8_t> encodeFramed(uint8_t opcode, const std::vector<uint8_t>& payload);

// A validated binary frame surfaced to downstream consumers (RDC's serial
// ingest, bound via setBinaryFrameHandler). payload holds the raw bytes
// between the opcode and the CRC.
struct Frame {
    uint8_t opcode;
    std::vector<uint8_t> payload;
};

using BinaryFrameHandler = std::function<void(const Frame&)>;

// Parses one jack's serial byte stream into framed binary frames
// (preamble 0xAA 0x55 + opcode + payload + CRC-16). One instance per jack.
// Binary framing because the jacks are physically unreliable (TRS contacts,
// partial insertion): a corrupted delimited string still parses as *some*
// string, while a frame failing CRC is dropped and the preamble resyncs.
// feed() runs on the UART event task; requestReset() is callable from the main
// loop and is honored at the start of the next feed(), so the parser's
// std::vector state is only ever mutated by the feeding thread.
class SerialFrameParser {
public:
    /// Consumes one burst of raw serial bytes; fires the frame handler for
    /// every CRC-valid frame completed within it.
    void feed(const uint8_t* data, size_t len);

    /// Request a parser reset from another thread. The actual clear happens at
    /// the start of the next feed(), keeping parser state single-owner.
    void requestReset() { resetRequested.store(true); }

    /// Registers the validated-frame consumer.
    void setBinaryFrameHandler(BinaryFrameHandler cb) { binaryFrameHandler = std::move(cb); }

private:
    enum class ParseState {
        SCAN_FOR_SYNC,
        GOT_AA,
        READING_OPCODE,
        READING_PAYLOAD,
        READING_CRC,
    };

    struct ParserState {
        ParseState state = ParseState::SCAN_FOR_SYNC;
        uint8_t opcode = 0;
        std::vector<uint8_t> payloadBuf;
        size_t expectedPayloadLen = 0;
        uint8_t crcBytes[2] = {0, 0};
        size_t crcBytesRead = 0;
    };

    ParserState parser;

    // Timestamp of the last arriving byte; checkTimeout() uses it to clear a
    // stalled mid-frame parse.
    unsigned long lastByteMs = 0;

    std::atomic<bool> resetRequested{false};

    BinaryFrameHandler binaryFrameHandler;

    // Wall-clock window after which a mid-frame parser resets to scan-for-sync.
    static constexpr unsigned long PARSER_TIMEOUT_MS = 50;

    void resetParser();
    void checkTimeout();
    static size_t opcodePayloadLen(uint8_t opcode);
    static unsigned long nowMs();
};
