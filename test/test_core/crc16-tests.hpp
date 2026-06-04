#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "wireless/crc16.hpp"

// CRC-16/CCITT XMODEM canonical check string ("123456789" -> 0x31C3). This is
// the standard identity vector for the variant; it pins poly/init/reflection so
// a refactor can't silently swap in a different CRC-16.
TEST(Crc16, canonicalCheckString) {
    const char* s = "123456789";
    uint16_t result = crc16(reinterpret_cast<const uint8_t*>(s), 9);
    EXPECT_EQ(result, 0x31C3);
}

// Regression vector over an 8-byte span (an opcode byte followed by a MAC), the
// shape of a short framed body. Value computed from the same algorithm; locks it
// against drift.
TEST(Crc16, eightByteSpanVector) {
    uint8_t input[] = {0x01, 0x10, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint16_t result = crc16(input, sizeof(input));
    EXPECT_EQ(result, 0x235D);
}

// Regression vector over a 12-byte span (two MACs), the shape of a longer body.
// Locks the algorithm against drift.
TEST(Crc16, twelveByteSpanVector) {
    uint8_t input[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                       0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint16_t result = crc16(input, sizeof(input));
    EXPECT_EQ(result, 0x239E);
}

// Empty input must produce the init value (0x0000) — guards against a
// future change that switches init to a non-zero value (XMODEM = 0x0000;
// CCITT-FALSE = 0xFFFF; KERMIT reflected = 0x0000 different algorithm).
TEST(Crc16, emptyInputReturnsInitValue) {
    uint16_t result = crc16(nullptr, 0);
    EXPECT_EQ(result, 0x0000);
}
