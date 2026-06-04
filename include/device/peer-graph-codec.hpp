#pragma once

#include "device/peer-graph.hpp"
#include <cstdint>
#include <optional>
#include <vector>

// Wire codec for the peer-graph protocol. Two framed opcodes share a common
// framing (preamble 0xAA 0x55, opcode, payload, CRC-16):
//   HELLO  (0x00): 1-hop direct-peer announcement, payload = source mac[6] +
//                  deviceType[1]. deviceType is a fixed hardware property
//                  (PDN/FDN) the game layer reads to pick the interaction
//                  (duel vs symbol); carried here because it is a device
//                  concern exchanged at connect, like the MAC.
//   BEACON (0x01): flooded topology frame, payload = source + inPeer + outPeer
namespace peer_graph {

constexpr uint8_t kOpHello = 0x00;
constexpr uint8_t kOpBeacon = 0x01;

struct HelloRecord {
    net::Mac source{};
    uint8_t deviceType = 0;
};

std::vector<uint8_t> encodeHello(const net::Mac& source, uint8_t deviceType);
std::optional<HelloRecord> decodeHello(const std::vector<uint8_t>& frame);

std::vector<uint8_t> encodeBeacon(const BeaconRecord& beacon);
std::optional<BeaconRecord> decodeBeacon(const std::vector<uint8_t>& frame);

}  // namespace peer_graph
