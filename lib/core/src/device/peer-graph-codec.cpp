#include "device/peer-graph-codec.hpp"

#include "wireless/crc16.hpp"
#include <algorithm>

namespace peer_graph {

namespace {

constexpr size_t kPreambleBytes = 2;
constexpr size_t kCrcBytes = 2;

std::vector<uint8_t> encodeFramed(uint8_t opcode, const std::vector<uint8_t>& payload) {
    // CRC covers opcode + payload, folded as two spans so the frame is built
    // in a single buffer (mirrors the parser's RX-side computation).
    uint16_t crc = crc16(&opcode, 1);
    crc = crc16Update(crc, payload.data(), payload.size());

    std::vector<uint8_t> frame;
    frame.reserve(kPreambleBytes + 1 + payload.size() + kCrcBytes);
    frame.push_back(kPreamble0);
    frame.push_back(kPreamble1);
    frame.push_back(opcode);
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    return frame;
}

}  // namespace

std::vector<uint8_t> encodeHello(const net::Mac& source, uint8_t deviceType,
                                 uint16_t userId) {
    std::vector<uint8_t> payload(source.begin(), source.end());
    payload.push_back(deviceType);
    payload.push_back(static_cast<uint8_t>(userId & 0xFF));
    payload.push_back(static_cast<uint8_t>((userId >> 8) & 0xFF));
    return encodeFramed(kOpHello, payload);
}

std::optional<HelloRecord> decodeHello(const std::vector<uint8_t>& payload) {
    if (payload.size() != kHelloPayloadLen) return std::nullopt;
    HelloRecord rec;
    std::copy_n(payload.begin(), 6, rec.source.begin());
    rec.deviceType = payload[6];
    rec.userId = static_cast<uint16_t>(payload[7]) |
                 (static_cast<uint16_t>(payload[8]) << 8);
    return rec;
}

std::vector<uint8_t> encodeBeacon(const BeaconRecord& beacon) {
    std::vector<uint8_t> payload;
    payload.reserve(kBeaconPayloadLen);
    payload.insert(payload.end(), beacon.source.begin(), beacon.source.end());
    payload.insert(payload.end(), beacon.inPeer.begin(), beacon.inPeer.end());
    payload.insert(payload.end(), beacon.outPeer.begin(), beacon.outPeer.end());
    payload.push_back(beacon.role);
    return encodeFramed(kOpBeacon, payload);
}

std::optional<BeaconRecord> decodeBeacon(const std::vector<uint8_t>& payload) {
    if (payload.size() != kBeaconPayloadLen) return std::nullopt;
    BeaconRecord b;
    std::copy_n(payload.begin(), 6, b.source.begin());
    std::copy_n(payload.begin() + 6, 6, b.inPeer.begin());
    std::copy_n(payload.begin() + 12, 6, b.outPeer.begin());
    b.role = payload[18];
    return b;
}

}  // namespace peer_graph
