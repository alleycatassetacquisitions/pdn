#include "device/serial-frame-parser.hpp"

#include "utils/simple-timer.hpp"
#include "wireless/crc16.hpp"

#include <cstring>
#include <utility>

std::array<uint8_t, HELLO_FRAME_LEN> encodeHelloFrame(const HelloPayload& hello) {
    const uint8_t opcode = OP_HELLO;
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(&hello);
    uint16_t crc = crc16(&opcode, 1);
    crc = crc16Update(crc, payload, sizeof(hello));

    std::array<uint8_t, HELLO_FRAME_LEN> frame{};
    frame[0] = FRAME_PREAMBLE_0;
    frame[1] = FRAME_PREAMBLE_1;
    frame[2] = opcode;
    std::memcpy(&frame[3], payload, sizeof(hello));
    frame[3 + sizeof(hello)] = static_cast<uint8_t>(crc >> 8);
    frame[3 + sizeof(hello) + 1] = static_cast<uint8_t>(crc & 0xFF);
    return frame;
}

std::vector<uint8_t> encodeFramed(const HelloPayload& hello) {
    const std::array<uint8_t, HELLO_FRAME_LEN> frame = encodeHelloFrame(hello);
    return std::vector<uint8_t>(frame.begin(), frame.end());
}

unsigned long SerialFrameParser::nowMs() {
    PlatformClock* clk = SimpleTimer::getPlatformClock();
    return clk != nullptr ? clk->milliseconds() : 0;
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
    std::vector<uint8_t> replay = parser.payloadBuf;
    replay.insert(replay.end(), parser.crcBytes, parser.crcBytes + parser.crcBytesRead);
    resetParser();
    // Re-entry depth is one: dropping the preamble and opcode leaves the replay
    // shorter than a whole frame, so it cannot reach another CRC comparison.
    for (uint8_t b : replay) {
        consumeByte(b);
    }
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
                if (got != expected) {
                    resyncFromConsumedBytes();
                    break;
                }
                if (helloFrameHandler && parser.payloadBuf.size() == sizeof(HelloPayload)) {
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
