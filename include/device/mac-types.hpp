#pragma once

#include <array>
#include <cstdint>

// 6-byte MAC address type and peer-validity helpers for the peer-graph protocol
// and its tests. The RDC and ESP-NOW layers still spell this type as a raw
// std::array<uint8_t, 6> / uint8_t[6]; converging them onto net::Mac is left to
// the peer-graph wire-up rather than done speculatively here.
namespace net {
using Mac = std::array<uint8_t, 6>;

inline bool isAllZeroMac(const Mac& m) {
    for (uint8_t b : m) if (b != 0x00) return false;
    return true;
}

inline bool isBroadcastMac(const Mac& m) {
    for (uint8_t b : m) if (b != 0xFF) return false;
    return true;
}

// Anti-poisoning: reject all-zero and broadcast MACs as direct peers.
inline bool isValidPeerMac(const Mac& m) {
    return !isAllZeroMac(m) && !isBroadcastMac(m);
}
}
