#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "wireless/crc16.hpp"

// CRC-16/CCITT XMODEM canonical check string. Locks the wire format against
// any future port (Python, Go, etc.) that needs to interoperate with serial
// frames produced by this code.
TEST(Crc16, canonicalCheckString) {
    const char* s = "123456789";
    uint16_t result = crc16(reinterpret_cast<const uint8_t*>(s), 9);
    EXPECT_EQ(result, 0x31C3);
}

// ADD frame body: opcode 0x01 + seqId 0x10 + MAC AA:BB:CC:DD:EE:FF.
// Locked against Python reference (poly 0x1021, init 0x0000, no reflection).
TEST(Crc16, frameVectorOpcodeAddPlusMac) {
    uint8_t input[] = {0x01, 0x10, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint16_t result = crc16(input, sizeof(input));
    EXPECT_EQ(result, 0x235D);
}

// Two MACs sorted by memcmp byte-order — locks the PROBE fingerprint
// computation against any peer port that needs to compute matching
// fingerprints from the same set.
TEST(Crc16, sortedProbeVectorTwoMacs) {
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
