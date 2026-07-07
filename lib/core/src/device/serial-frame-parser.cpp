#include "device/serial-frame-parser.hpp"

#include "utils/simple-timer.hpp"
#include "wireless/crc16.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace {
constexpr size_t PREAMBLE_BYTES = 2;
constexpr size_t CRC_BYTES = 2;
}

std::vector<uint8_t> encodeFramed(uint8_t opcode, const std::vector<uint8_t>& payload) {
    // CRC covers opcode + payload, folded as two spans so the frame is built
    // in a single buffer (mirrors the parser's RX-side computation).
    uint16_t crc = crc16(&opcode, 1);
    crc = crc16Update(crc, payload.data(), payload.size());

    std::vector<uint8_t> frame;
    frame.reserve(PREAMBLE_BYTES + 1 + payload.size() + CRC_BYTES);
    frame.push_back(FRAME_PREAMBLE_0);
    frame.push_back(FRAME_PREAMBLE_1);
    frame.push_back(opcode);
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    return frame;
}

std::vector<uint8_t> encodeFramed(const HelloPayload& hello) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&hello);
    return encodeFramed(OP_HELLO, std::vector<uint8_t>(bytes, bytes + sizeof(hello)));
}

unsigned long SerialFrameParser::nowMs() {
    PlatformClock* clk = SimpleTimer::getPlatformClock();
    if (clk == nullptr) {
        // A null clock must fail loud, not silently disable timeouts: max()
        // forces the next gap computation to time out immediately.
        return std::numeric_limits<unsigned long>::max();
    }
    return clk->milliseconds();
}

size_t SerialFrameParser::opcodePayloadLen(uint8_t opcode) {
    switch (opcode) {
        case OP_HELLO: return HELLO_PAYLOAD_LEN;
        default:       return 0;
    }
}

void SerialFrameParser::resetParser() {
    parser.state = ParseState::SCAN_FOR_SYNC;
    parser.opcode = 0;
    parser.payloadBuf.clear();
    parser.expectedPayloadLen = 0;
    parser.crcBytes[0] = 0;
    parser.crcBytes[1] = 0;
    parser.crcBytesRead = 0;
}

void SerialFrameParser::checkTimeout() {
    if (parser.state == ParseState::SCAN_FOR_SYNC) return;
    const unsigned long now = nowMs();
    // Clamp a backwards clock (now < lastByteMs) to gap 0 so unsigned
    // subtraction can't underflow to ~UINT_MAX and fire a spurious mid-frame
    // resync. The null-clock case still times out: nowMs() returns max() there,
    // which is never below lastByteMs, so it yields a large gap unclamped.
    const unsigned long gap = now >= lastByteMs ? now - lastByteMs : 0;
    if (gap > PARSER_TIMEOUT_MS) {
        resetParser();
    }
}

void SerialFrameParser::feed(const uint8_t* data, size_t len) {
    // Honor a reset requested from another thread before consuming this burst,
    // so the only thread that ever clears the parser is this one. Then check the
    // mid-frame timeout, also before consuming, so a stale partial frame is
    // cleared before a late byte completes a corrupt one.
    if (resetRequested.exchange(false)) {
        resetParser();
    }
    checkTimeout();
    const unsigned long now = nowMs();
    lastByteMs = now;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        switch (parser.state) {
            case ParseState::SCAN_FOR_SYNC:
                if (b == FRAME_PREAMBLE_0) {
                    parser.state = ParseState::GOT_AA;
                }
                break;
            case ParseState::GOT_AA:
                if (b == FRAME_PREAMBLE_1) {
                    parser.state = ParseState::READING_OPCODE;
                } else if (b == FRAME_PREAMBLE_0) {
                    // Stay in GOT_AA: a stray 0xAA followed by the real preamble (0xAA 0x55) should
                    // still detect the frame start. Going back to SCAN_FOR_SYNC would drop the second
                    // 0xAA, requiring a 3-byte preamble before re-syncing.
                } else {
                    resetParser();
                }
                break;
            case ParseState::READING_OPCODE:
                if (b != OP_HELLO) {  // HELLO is the sole serial opcode
                    resetParser();
                    break;
                }
                parser.opcode = b;
                parser.expectedPayloadLen = opcodePayloadLen(b);
                parser.payloadBuf.clear();
                if (parser.expectedPayloadLen == 0) {
                    parser.state = ParseState::READING_CRC;
                    parser.crcBytesRead = 0;
                } else {
                    parser.state = ParseState::READING_PAYLOAD;
                }
                break;
            case ParseState::READING_PAYLOAD:
                parser.payloadBuf.push_back(b);
                if (parser.payloadBuf.size() >= parser.expectedPayloadLen) {
                    parser.state = ParseState::READING_CRC;
                    parser.crcBytesRead = 0;
                }
                break;
            case ParseState::READING_CRC:
                parser.crcBytes[parser.crcBytesRead++] = b;
                if (parser.crcBytesRead == 2) {
                    const uint16_t got = (static_cast<uint16_t>(parser.crcBytes[0]) << 8) | static_cast<uint16_t>(parser.crcBytes[1]);
                    // CRC covers opcode + payload; fold the two spans into a
                    // running CRC to avoid a per-frame copy on the UART event task.
                    uint16_t expected = crc16(&parser.opcode, 1);
                    expected = crc16Update(expected, parser.payloadBuf.data(),
                                           parser.payloadBuf.size());
                    if (got == expected && helloFrameHandler &&
                        parser.payloadBuf.size() == sizeof(HelloPayload)) {
                        // The packed struct mirrors the wire byte-for-byte, so
                        // the validated payload copies straight in.
                        HelloPayload hello;
                        std::memcpy(&hello, parser.payloadBuf.data(), sizeof(hello));
                        helloFrameHandler(hello);
                    }
                    resetParser();
                }
                break;
        }
    }
}
