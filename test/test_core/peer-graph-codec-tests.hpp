#pragma once

#include <gtest/gtest.h>
#include <vector>
#include "device/peer-graph-codec.hpp"

// decode*() consumes the payload a SerialFrameParser hands back after stripping
// and CRC-checking the framing. Recover that payload from a full encoded frame
// (drop the 2-byte preamble + 1-byte opcode prefix and the 2-byte CRC suffix) so
// these round-trips exercise the same decoder the receive path uses. Framing and
// CRC rejection are the parser's concern, covered by the serial-frame-parser tests.
inline std::vector<uint8_t> codecPayloadOf(const std::vector<uint8_t>& frame) {
    return std::vector<uint8_t>(frame.begin() + 3, frame.end() - 2);
}

inline void codecHelloRoundTrip() {
    net::Mac src = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    auto frame = peer_graph::encodeHello(src, 2, 8888);  // deviceType FDN=2, id 8888
    EXPECT_EQ(frame.size(), 14u);  // preamble 2 + opcode 1 + payload 9 + CRC 2
    auto decoded = peer_graph::decodeHello(codecPayloadOf(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->source, src);
    EXPECT_EQ(decoded->deviceType, 2);
    EXPECT_EQ(decoded->userId, 8888);
}

inline void codecBeaconRoundTrip() {
    BeaconRecord b{{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}};
    b.role = 1;
    auto frame = peer_graph::encodeBeacon(b);
    EXPECT_EQ(frame.size(), 24u);  // preamble 2 + opcode 1 + payload 19 + CRC 2
    auto decoded = peer_graph::decodeBeacon(codecPayloadOf(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, b);
    EXPECT_EQ(decoded->role, 1);
}

// The role byte is opaque to the codec: any value must survive the wire
// verbatim (a bool-normalizing encoder would crush the tri-state encoding).
inline void codecBeaconRoleByteIsOpaque() {
    BeaconRecord b{{0x01, 0, 0, 0, 0, 0}, {}, {}};
    b.role = 2;
    auto decoded = peer_graph::decodeBeacon(
        codecPayloadOf(peer_graph::encodeBeacon(b)));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->role, 2);
    EXPECT_EQ(*decoded, b);
}

inline void codecBeaconEmptyPeersRoundTrip() {
    BeaconRecord b{{0x01, 0, 0, 0, 0, 0}, {}, {}};
    auto frame = peer_graph::encodeBeacon(b);
    auto decoded = peer_graph::decodeBeacon(codecPayloadOf(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, b);
}

inline void codecRejectsWrongLength() {
    // decode*() validates payload length (the parser owns framing + CRC). A
    // payload sized for the other opcode, or an empty payload, is rejected.
    net::Mac src = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    BeaconRecord b{{0x01, 0, 0, 0, 0, 0}, {}, {}};
    EXPECT_FALSE(peer_graph::decodeHello(
        codecPayloadOf(peer_graph::encodeBeacon(b))).has_value());      // 19 bytes into HELLO
    EXPECT_FALSE(peer_graph::decodeBeacon(
        codecPayloadOf(peer_graph::encodeHello(src, 1,
            peer_graph::kUserIdUnknown))).has_value());                 // 9 bytes into BEACON
    EXPECT_FALSE(peer_graph::decodeHello({}).has_value());
    EXPECT_FALSE(peer_graph::decodeBeacon({}).has_value());
}
