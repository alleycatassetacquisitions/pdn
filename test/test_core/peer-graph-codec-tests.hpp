#pragma once

#include <gtest/gtest.h>
#include <vector>
#include "device/peer-graph-codec.hpp"
#include "wireless/crc16.hpp"

inline void codecHelloRoundTrip() {
    net::Mac src = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    auto frame = peer_graph::encodeHello(src, 2);  // deviceType FDN=2
    EXPECT_EQ(frame.size(), 12u);  // preamble 2 + opcode 1 + payload 7 + CRC 2
    auto decoded = peer_graph::decodeHello(frame);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->source, src);
    EXPECT_EQ(decoded->deviceType, 2);
}

inline void codecBeaconRoundTrip() {
    BeaconRecord b{{0x01, 0, 0, 0, 0, 0}, {0x02, 0, 0, 0, 0, 0}, {0x03, 0, 0, 0, 0, 0}};
    auto frame = peer_graph::encodeBeacon(b);
    EXPECT_EQ(frame.size(), 23u);  // preamble 2 + opcode 1 + payload 18 + CRC 2
    auto decoded = peer_graph::decodeBeacon(frame);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, b);
}

inline void codecBeaconEmptyPeersRoundTrip() {
    BeaconRecord b{{0x01, 0, 0, 0, 0, 0}, {}, {}};
    auto frame = peer_graph::encodeBeacon(b);
    auto decoded = peer_graph::decodeBeacon(frame);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, b);
}

inline void codecRejectsCorruptedCrc() {
    net::Mac src = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    auto frame = peer_graph::encodeHello(src, 1);
    frame[5] ^= 0xFF;  // flip a payload byte; CRC no longer matches
    EXPECT_FALSE(peer_graph::decodeHello(frame).has_value());
}

// Builds a well-framed, valid-CRC frame carrying an arbitrary opcode over a
// payload of the given length, so length and CRC both pass validation and only
// the opcode check can reject it.
inline std::vector<uint8_t> codecFrameWithOpcode(uint8_t opcode, size_t payloadLen) {
    std::vector<uint8_t> body;
    body.push_back(opcode);
    for (size_t i = 0; i < payloadLen; ++i) body.push_back(static_cast<uint8_t>(i));
    const uint16_t crc = crc16(body.data(), body.size());
    std::vector<uint8_t> frame{0xAA, 0x55};
    frame.insert(frame.end(), body.begin(), body.end());
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    return frame;
}

inline void codecRejectsWrongOpcode() {
    // Cross-decoding a whole BEACON/HELLO frame is rejected on the length check
    // (payloads differ: 18 vs 7), so it never reaches the opcode comparison.
    BeaconRecord b{{0x01, 0, 0, 0, 0, 0}, {}, {}};
    EXPECT_FALSE(peer_graph::decodeHello(peer_graph::encodeBeacon(b)).has_value());
    net::Mac src = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    EXPECT_FALSE(peer_graph::decodeBeacon(peer_graph::encodeHello(src, 1)).has_value());

    // Isolate the opcode guard: a frame with HELLO's payload length (7) but the
    // BEACON opcode, with a valid CRC, passes length+CRC and can only be rejected
    // by the opcode check. Same for a BEACON-length frame carrying the HELLO opcode.
    EXPECT_FALSE(peer_graph::decodeHello(
        codecFrameWithOpcode(peer_graph::kOpBeacon, 7)).has_value());
    EXPECT_FALSE(peer_graph::decodeBeacon(
        codecFrameWithOpcode(peer_graph::kOpHello, 18)).has_value());

    // Sanity: the same builder with the CORRECT opcode decodes, proving the
    // rejections above are due to the opcode and not the hand-built framing.
    EXPECT_TRUE(peer_graph::decodeHello(
        codecFrameWithOpcode(peer_graph::kOpHello, 7)).has_value());
}
