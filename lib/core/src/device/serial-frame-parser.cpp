#include "device/serial-frame-parser.hpp"

#include "utils/simple-timer.hpp"
#include "utils/secure-wipe.hpp"
#include "wireless/crc16.hpp"

#include <cstring>
#include <utility>

// Lays one frame into `out`, which holds `length` + 5 bytes: preamble, opcode,
// payload, then the CRC-16 over opcode + payload, high byte first.
static void encodeFrame(uint8_t opcode, const uint8_t* payload, size_t length, uint8_t* out) {
    uint16_t crc = crc16(&opcode, 1);
    crc = crc16Update(crc, payload, length);

    out[0] = FRAME_PREAMBLE_0;
    out[1] = FRAME_PREAMBLE_1;
    out[2] = opcode;
    std::memcpy(&out[3], payload, length);
    out[3 + length] = static_cast<uint8_t>(crc >> 8);
    out[3 + length + 1] = static_cast<uint8_t>(crc & 0xFF);
}

std::array<uint8_t, HELLO_FRAME_LEN> encodeHelloFrame(const HelloPayload& hello) {
    std::array<uint8_t, HELLO_FRAME_LEN> frame{};
    encodeFrame(OP_HELLO, reinterpret_cast<const uint8_t*>(&hello), sizeof(hello), frame.data());
    return frame;
}

std::vector<uint8_t> encodeFramed(const HelloPayload& hello) {
    const std::array<uint8_t, HELLO_FRAME_LEN> frame = encodeHelloFrame(hello);
    return std::vector<uint8_t>(frame.begin(), frame.end());
}

void encodeApplicationFrame(const ApplicationPayload& payload, ApplicationFrame& frame) {
    encodeFrame(OP_APPLICATION, payload.data(), payload.size(), frame.data());
}

unsigned long SerialFrameParser::nowMs() {
    PlatformClock* clk = SimpleTimer::getPlatformClock();
    return clk != nullptr ? clk->milliseconds() : 0;
}

bool SerialFrameParser::isKnownOpcode(uint8_t opcode, size_t& payloadLen) {
    switch (opcode) {
        case OP_HELLO: payloadLen = HELLO_PAYLOAD_LEN; return true;
        case OP_APPLICATION: payloadLen = APPLICATION_PAYLOAD_LEN; return true;
        default: return false;
    }
}

void SerialFrameParser::resetParser() {
    parser.state = ParseState::SCAN_FOR_SYNC;
    parser.opcode = 0;
    // An application payload can be a secret, and clear() leaves the bytes in place.
    secureWipe(parser.payloadBuf.data(), parser.payloadBuf.size());
    parser.payloadBuf.clear();
    parser.expectedPayloadLen = 0;
    parser.crcBytes[0] = 0;
    parser.crcBytes[1] = 0;
    parser.crcBytesRead = 0;
}

void SerialFrameParser::checkTimeout() {
    if (parser.state == ParseState::SCAN_FOR_SYNC) return;
    if (nowMs() - lastByteMs > PARSER_TIMEOUT_MS) {
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
    lastByteMs = nowMs();
    for (size_t i = 0; i < len; ++i) {
        consumeByte(data[i]);
    }
}

void SerialFrameParser::resyncFromConsumedBytes() {
    // A CRC failure means the frame boundary is wrong, not merely that a byte
    // flipped: a truncated frame keeps consuming into the next one, so throwing
    // the consumed bytes away costs two frames per dropped byte instead of one.
    // The driver drains the whole RX FIFO in a single loop, so there need be no
    // inter-frame gap for the mid-frame timeout to catch that. Replaying the
    // bytes after the (already disproven) preamble and opcode lets an embedded
    // 0xAA 0x55 restart framing at the real frame start; a trailing lone 0xAA
    // ends in GOT_AA and pairs with whatever byte arrives next.
    // Sized before it is filled, so the wipe below reaches every copy it made.
    std::vector<uint8_t> replay;
    replay.reserve(parser.payloadBuf.size() + parser.crcBytesRead);
    replay.insert(replay.end(), parser.payloadBuf.begin(), parser.payloadBuf.end());
    replay.insert(replay.end(), parser.crcBytes, parser.crcBytes + parser.crcBytesRead);
    resetParser();
    // Re-entry depth is at most two; the outer replay may complete further frames.
    for (uint8_t b : replay) {
        consumeByte(b);
    }
    secureWipe(replay.data(), replay.size());
}

void SerialFrameParser::consumeByte(uint8_t b) {
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
            if (!isKnownOpcode(b, parser.expectedPayloadLen)) {
                resetParser();
                break;
            }
            parser.opcode = b;
            // Sized up front: growing byte by byte would leave copies of the
            // payload's start in freed heap blocks that no wipe can reach.
            parser.payloadBuf.clear();
            parser.payloadBuf.reserve(parser.expectedPayloadLen);
            parser.state = ParseState::READING_PAYLOAD;
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
                // CRC covers opcode + payload; folded as a running CRC to avoid
                // a per-frame copy.
                uint16_t expected = crc16(&parser.opcode, 1);
                expected = crc16Update(expected, parser.payloadBuf.data(),
                                       parser.payloadBuf.size());
                if (got != expected) {
                    resyncFromConsumedBytes();
                    break;
                }
                dispatchFrame();
                resetParser();
            }
            break;
    }
}

void SerialFrameParser::dispatchFrame() {
    // The payload is as long as isKnownOpcode() gave for this opcode, and each
    // destination below is that size by its type.
    switch (parser.opcode) {
        case OP_HELLO:
            if (helloFrameHandler) {
                // The packed struct mirrors the wire byte-for-byte, so
                // the validated payload copies straight in.
                HelloPayload hello;
                std::memcpy(&hello, parser.payloadBuf.data(), sizeof(hello));
                helloFrameHandler(hello);
            }
            break;
        case OP_APPLICATION:
            if (applicationFrameHandler) {
                ApplicationPayload payload;
                std::memcpy(payload.data(), parser.payloadBuf.data(), payload.size());
                applicationFrameHandler(payload);
                secureWipe(payload.data(), payload.size());
            }
            break;
    }
}
