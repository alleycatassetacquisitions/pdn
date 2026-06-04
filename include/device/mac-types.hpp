#pragma once

#include <array>
#include <cstdint>

// Shared 6-byte MAC address type and peer-validity helpers, used by the
// peer-graph protocol, the RDC, and their tests.
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
