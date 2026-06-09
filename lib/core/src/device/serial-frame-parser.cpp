#include "device/serial-frame-parser.hpp"

#include "device/peer-graph-codec.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/crc16.hpp"

#include <limits>
#include <utility>

unsigned long SerialFrameParser::nowMs() {
    auto* clk = SimpleTimer::getPlatformClock();
    if (clk == nullptr) {
        // A null clock must fail loud, not silently disable timeouts: max()
        // forces the next gap computation to time out immediately.
        return std::numeric_limits<unsigned long>::max();
    }
    return clk->milliseconds();
}

size_t SerialFrameParser::opcodePayloadLen(uint8_t opcode) {
    switch (opcode) {
        case peer_graph::kOpHello:  return peer_graph::kHelloPayloadLen;
        case peer_graph::kOpBeacon: return peer_graph::kBeaconPayloadLen;
        default:                    return 0;
    }
}

void SerialFrameParser::resetParser() {
    parser_.state = ParseState::ScanForSync;
    parser_.opcode = 0;
    parser_.payloadBuf.clear();
    parser_.expectedPayloadLen = 0;
    parser_.crcBytes[0] = 0;
    parser_.crcBytes[1] = 0;
    parser_.crcBytesRead = 0;
}

void SerialFrameParser::checkTimeout() {
    if (parser_.state == ParseState::ScanForSync) return;
    const unsigned long now = nowMs();
    // Clamp a backwards clock (now < lastByteMs_) to gap 0 so unsigned
    // subtraction can't underflow to ~UINT_MAX and fire a spurious mid-frame
    // resync. The null-clock case still times out: nowMs() returns max() there,
    // which is never below lastByteMs_, so it yields a large gap unclamped.
    // Mirrors the gap guards in PeerGraph and RemoteDeviceCoordinator.
    const unsigned long gap = now >= lastByteMs_ ? now - lastByteMs_ : 0;
    if (gap > kParserTimeoutMs) {
        resetParser();
    }
}

void SerialFrameParser::feed(const uint8_t* data, size_t len) {
    // Honor a reset requested from another thread before consuming this burst,
    // so the only thread that ever clears the parser is this one. Then check the
    // mid-frame timeout, also before consuming, so a stale partial frame is
    // cleared before a late byte completes a corrupt one.
    if (resetRequested_.exchange(false)) {
        resetParser();
    }
    checkTimeout();
    const unsigned long now = nowMs();
    lastByteMs_ = now;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        switch (parser_.state) {
            case ParseState::ScanForSync:
                if (b == peer_graph::kPreamble0) {
                    parser_.state = ParseState::GotAA;
                }
                break;
            case ParseState::GotAA:
                if (b == peer_graph::kPreamble1) {
                    parser_.state = ParseState::ReadingOpcode;
                } else if (b == peer_graph::kPreamble0) {
                    // Stay in GotAA: a stray 0xAA followed by the real preamble (0xAA 0x55) should
                    // still detect the frame start. Going back to ScanForSync would drop the second
                    // 0xAA, requiring a 3-byte preamble before re-syncing.
                } else {
                    resetParser();
                }
                break;
            case ParseState::ReadingOpcode:
                if (b > peer_graph::kOpBeacon) {  // only HELLO/BEACON opcodes exist
                    resetParser();
                    break;
                }
                parser_.opcode = b;
                parser_.expectedPayloadLen = opcodePayloadLen(b);
                parser_.payloadBuf.clear();
                if (parser_.expectedPayloadLen == 0) {
                    parser_.state = ParseState::ReadingCrc;
                    parser_.crcBytesRead = 0;
                } else {
                    parser_.state = ParseState::ReadingPayload;
                }
                break;
            case ParseState::ReadingPayload:
                parser_.payloadBuf.push_back(b);
                if (parser_.payloadBuf.size() >= parser_.expectedPayloadLen) {
                    parser_.state = ParseState::ReadingCrc;
                    parser_.crcBytesRead = 0;
                }
                break;
            case ParseState::ReadingCrc:
                parser_.crcBytes[parser_.crcBytesRead++] = b;
                if (parser_.crcBytesRead == 2) {
                    const uint16_t got = (static_cast<uint16_t>(parser_.crcBytes[0]) << 8)
                                       | static_cast<uint16_t>(parser_.crcBytes[1]);
                    // CRC covers opcode + payload; fold the two spans into a
                    // running CRC to avoid a per-frame copy on the UART event task.
                    uint16_t expected = crc16(&parser_.opcode, 1);
                    expected = crc16Update(expected, parser_.payloadBuf.data(),
                                           parser_.payloadBuf.size());
                    if (got == expected && binaryFrameHandler_) {
                        // Move, don't copy: resetParser()'s clear() is valid on
                        // the moved-from buffer.
                        Frame f{parser_.opcode, std::move(parser_.payloadBuf)};
                        binaryFrameHandler_(f);
                    }
                    resetParser();
                }
                break;
        }
    }
}
