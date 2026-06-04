#include "device/peer-graph-codec.hpp"

#include "wireless/crc16.hpp"
#include <algorithm>
#include <array>

namespace peer_graph {

namespace {

constexpr size_t kPreambleBytes = 2;
constexpr size_t kCrcBytes = 2;
constexpr uint8_t kPreamble0 = 0xAA;
constexpr uint8_t kPreamble1 = 0x55;

// Assemble the whole frame in one buffer: preamble, opcode, payload, CRC. The
// opcode+payload sit contiguously at frame[2..], so the CRC is taken over that
// slice in place — no separate body buffer to keep byte-aligned with decode.
std::vector<uint8_t> encodeFramed(uint8_t opcode, const uint8_t* payload, size_t payloadLen) {
    std::vector<uint8_t> frame;
    frame.reserve(kPreambleBytes + 1 + payloadLen + kCrcBytes);
    frame.push_back(kPreamble0);
    frame.push_back(kPreamble1);
    frame.push_back(opcode);
    frame.insert(frame.end(), payload, payload + payloadLen);
    const uint16_t crc = crc16(&frame[2], 1 + payloadLen);
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    return frame;
}

// Validate framing + opcode + CRC. Returns a pointer to the payload start and
// its length via out-params, or false on any mismatch.
bool validateFrame(const std::vector<uint8_t>& frame, uint8_t expectedOpcode,
                   size_t expectedPayloadLen, const uint8_t** payloadOut) {
    const size_t expectedFrameLen =
        kPreambleBytes + 1 /*opcode*/ + expectedPayloadLen + kCrcBytes;
    if (frame.size() != expectedFrameLen) return false;
    if (frame[0] != kPreamble0 || frame[1] != kPreamble1) return false;
    if (frame[2] != expectedOpcode) return false;
    const size_t bodyLen = 1 + expectedPayloadLen;
    const uint16_t got =
        (static_cast<uint16_t>(frame[2 + bodyLen]) << 8) | frame[2 + bodyLen + 1];
    const uint16_t expected = crc16(&frame[2], bodyLen);
    if (got != expected) return false;
    *payloadOut = &frame[3];
    return true;
}

}  // namespace

std::vector<uint8_t> encodeHello(const net::Mac& source, uint8_t deviceType) {
    std::array<uint8_t, 7> payload;
    std::copy(source.begin(), source.end(), payload.begin());
    payload[6] = deviceType;
    return encodeFramed(kOpHello, payload.data(), payload.size());
}

std::optional<HelloRecord> decodeHello(const std::vector<uint8_t>& frame) {
    const uint8_t* payload = nullptr;
    if (!validateFrame(frame, kOpHello, 7, &payload)) return std::nullopt;
    HelloRecord rec;
    std::copy_n(payload, 6, rec.source.begin());
    rec.deviceType = payload[6];
    return rec;
}

std::vector<uint8_t> encodeBeacon(const BeaconRecord& beacon) {
    std::array<uint8_t, 18> payload;
    std::copy(beacon.source.begin(), beacon.source.end(), payload.begin());
    std::copy(beacon.inPeer.begin(), beacon.inPeer.end(), payload.begin() + 6);
    std::copy(beacon.outPeer.begin(), beacon.outPeer.end(), payload.begin() + 12);
    return encodeFramed(kOpBeacon, payload.data(), payload.size());
}

std::optional<BeaconRecord> decodeBeacon(const std::vector<uint8_t>& frame) {
    const uint8_t* payload = nullptr;
    if (!validateFrame(frame, kOpBeacon, 18, &payload)) return std::nullopt;
    BeaconRecord b;
    std::copy_n(payload, 6, b.source.begin());
    std::copy_n(payload + 6, 6, b.inPeer.begin());
    std::copy_n(payload + 12, 6, b.outPeer.begin());
    return b;
}

}  // namespace peer_graph
