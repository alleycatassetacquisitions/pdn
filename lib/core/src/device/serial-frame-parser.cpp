#include "device/serial-frame-parser.hpp"

#include "utils/secure-wipe.hpp"
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

size_t encodeApplicationFrame(const ApplicationFrameHeader& header, const uint8_t* payload,
                              ApplicationFrame& frame) {
    const size_t length = header.length;
    if (length > APPLICATION_PAYLOAD_MAX_LEN) return 0;
    frame[0] = FRAME_PREAMBLE_0;
    frame[1] = FRAME_PREAMBLE_1;
    frame[2] = OP_APPLICATION;
    std::memcpy(&frame[3], &header, APPLICATION_HEADER_LEN);
    if (length > 0) std::memcpy(&frame[3 + APPLICATION_HEADER_LEN], payload, length);
    // CRC over opcode + header + payload, high byte first, as for HELLO.
    const size_t crcAt = 3 + APPLICATION_HEADER_LEN + length;
    const uint16_t crc = crc16(&frame[2], crcAt - 2);
    frame[crcAt] = static_cast<uint8_t>(crc >> 8);
    frame[crcAt + 1] = static_cast<uint8_t>(crc & 0xFF);
    return crcAt + 2;
}

unsigned long SerialFrameParser::nowMs() {
    PlatformClock* clk = SimpleTimer::getPlatformClock();
    return clk != nullptr ? clk->milliseconds() : 0;
}

size_t SerialFrameParser::opcodePayloadLen(uint8_t opcode) {
    switch (opcode) {
        case OP_HELLO: return HELLO_PAYLOAD_LEN;
        case OP_APPLICATION: return APPLICATION_HEADER_LEN;
        default: return 0;
    }
}

void SerialFrameParser::resetParser() {
    parser.state = ParseState::SCAN_FOR_SYNC;
    parser.opcode = 0;
    // An application payload may be a key, so it does not outlive its frame.
    secureWipe(parser.payloadBuf.data(), parser.payloadLength);
    parser.payloadLength = 0;
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
    std::array<uint8_t, PAYLOAD_CAPACITY + 2> replay;
    const size_t replayLength = parser.payloadLength + parser.crcBytesRead;
    std::memcpy(replay.data(), parser.payloadBuf.data(), parser.payloadLength);
    std::memcpy(replay.data() + parser.payloadLength, parser.crcBytes, parser.crcBytesRead);
    resetParser();
    // Each replay drops the preamble and opcode it disproved, so a nested one
    // is shorter than the replay that started it and the recursion ends.
    for (size_t i = 0; i < replayLength; ++i) {
        consumeByte(replay[i]);
    }
    secureWipe(replay.data(), replayLength);
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
            parser.expectedPayloadLen = opcodePayloadLen(b);
            if (parser.expectedPayloadLen == 0) {
                resetParser();
                break;
            }
            parser.opcode = b;
            parser.payloadLength = 0;
            parser.state = ParseState::READING_PAYLOAD;
            break;
        case ParseState::READING_PAYLOAD:
            parser.payloadBuf[parser.payloadLength++] = b;
            if (parser.opcode == OP_APPLICATION && parser.payloadLength == APPLICATION_HEADER_LEN) {
                // The length byte ends the header and frames the rest. A longer
                // claim is no frame of ours, so the bytes are scanned afresh.
                const size_t length = parser.payloadBuf[APPLICATION_HEADER_LEN - 1];
                if (length > APPLICATION_PAYLOAD_MAX_LEN) {
                    resyncFromConsumedBytes();
                    break;
                }
                parser.expectedPayloadLen = APPLICATION_HEADER_LEN + length;
            }
            if (parser.payloadLength >= parser.expectedPayloadLen) {
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
                expected = crc16Update(expected, parser.payloadBuf.data(), parser.payloadLength);
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
    // The payload is exactly as long as its opcode framed it.
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
                ApplicationFrameHeader header;
                std::memcpy(&header, parser.payloadBuf.data(), APPLICATION_HEADER_LEN);
                applicationFrameHandler(header, parser.payloadBuf.data() + APPLICATION_HEADER_LEN);
            }
            break;
        default: break;
    }
}
