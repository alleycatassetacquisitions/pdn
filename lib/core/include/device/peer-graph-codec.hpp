#pragma once

#include "device/peer-graph.hpp"
#include <cstdint>
#include <optional>
#include <vector>

// Wire codec for the peer-graph protocol. Two framed opcodes share a common
// framing (preamble 0xAA 0x55, opcode, payload, CRC-16):
//   HELLO  (0x00): 1-hop direct-peer announcement, payload = source mac[6] +
//                  deviceType[1] + userId[2]. deviceType is a fixed hardware
//                  property (PDN/FDN) the game layer reads to pick the
//                  interaction (duel vs symbol). userId is the 4-digit player
//                  code (0-9999) packed little-endian; 0xFFFF means not yet
//                  registered. Both are device/endpoint facts exchanged at
//                  connect, like the MAC.
//   BEACON (0x01): flooded topology frame, payload = source + inPeer + outPeer
//                  + role[1], an opaque game byte carried verbatim (see
//                  BeaconRecord::role for why it rides this flood).
//
// encode*() produces a full framed packet for transmission. decode*() is the
// inverse over the *payload* a SerialFrameParser hands back once it has stripped
// and CRC-checked the framing, so framing/CRC live in exactly one place (the
// parser) and payload<->record in exactly one place (here).
namespace peer_graph {

constexpr uint8_t kOpHello = 0x00;
constexpr uint8_t kOpBeacon = 0x01;

// Frame preamble; the encoder here and SerialFrameParser's sync scan must
// agree, so both read these.
constexpr uint8_t kPreamble0 = 0xAA;
constexpr uint8_t kPreamble1 = 0x55;

// Payload lengths (bytes after the opcode, before the CRC). The single source of
// truth for both the encoder here and SerialFrameParser's length checks.
constexpr size_t kHelloPayloadLen = 9;   // source mac[6] + deviceType[1] + userId[2]
constexpr size_t kBeaconPayloadLen = 19;  // source[6] + inPeer[6] + outPeer[6] + role[1]

// 0xFFFF: peer has no registered 4-digit id yet (pre-pairing placeholder).
constexpr uint16_t kUserIdUnknown = 0xFFFF;

struct HelloRecord {
    net::Mac source{};
    uint8_t deviceType = 0;
    uint16_t userId = kUserIdUnknown;
};

std::vector<uint8_t> encodeHello(const net::Mac& source, uint8_t deviceType,
                                 uint16_t userId);
std::optional<HelloRecord> decodeHello(const std::vector<uint8_t>& payload);

std::vector<uint8_t> encodeBeacon(const BeaconRecord& beacon);
std::optional<BeaconRecord> decodeBeacon(const std::vector<uint8_t>& payload);

}  // namespace peer_graph
