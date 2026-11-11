#pragma once

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include "game/match.hpp"
#include "id-generator.hpp"

class MatchTestSuite : public testing::Test {
protected:
    void SetUp() override {
    }
};

// ============================================
// Construction & ID Tests
// ============================================

// Match() is defaulted, so the member initializers are the only thing that
// empties a fresh match. Nothing else in the suite reads that state.
inline void matchDefaultConstructionIsEmpty() {
    Match match;

    EXPECT_STREQ(match.getMatchId(), "");
    EXPECT_EQ(match.getHunterDrawTime(), 0UL);
    EXPECT_EQ(match.getBountyDrawTime(), 0UL);
}

// A shootout id is "SHT-" then 32 digits: 36 characters with one hyphen where a
// UUID has four. uuidStringToBytes packs two hex characters per byte and skips
// hyphens, so this id yields 18 bytes. The canary catches a write past 16.
inline void matchShootoutIdConversionStaysInBounds() {
    char shootoutId[IdGenerator::UUID_BUFFER_SIZE];
    snprintf(shootoutId, sizeof(shootoutId), "SHT-%032d", 3);
    ASSERT_EQ(strlen(shootoutId), IdGenerator::UUID_STRING_LENGTH);

    struct {
        uint8_t bytes[IdGenerator::UUID_BINARY_SIZE];
        uint8_t canary[4];
    } probe;
    static_assert(sizeof(probe) == IdGenerator::UUID_BINARY_SIZE + 4,
                  "canary must sit directly after the field for this to detect anything");
    memset(&probe, 0xAA, sizeof(probe));

    IdGenerator::uuidStringToBytes(shootoutId, probe.bytes);

    for (size_t i = 0; i < sizeof(probe.canary); i++) {
        EXPECT_EQ(probe.canary[i], 0xAA) << "wrote past the uuid field at index " << i;
    }
}

// ============================================
// Binary Serialization Tests
// ============================================

inline void matchBinarySizeIsCorrect() {
    // Fixed so a 64-bit native build and the ESP32 read each other's records.
    EXPECT_EQ(Match::binarySize(), 112u);
}

inline void matchBinaryRoundTripPreservesUuidsWithEmbeddedZeroBytes() {
    UUIDBytes matchUuid{};
    UUIDBytes hunterPlayerId{};
    UUIDBytes bountyPlayerId{};
    UUIDBytes hunterTeamId{};
    UUIDBytes bountyTeamId{};
    UUIDBytes winnerPlayerId{};

    const uint8_t matchRaw[16] = {0x00, 0x11, 0x00, 0x22, 0x33, 0x00, 0x44, 0x55,
                                  0x66, 0x00, 0x77, 0x88, 0x99, 0x00, 0xAA, 0xBB};
    const uint8_t hunterRaw[16] = {0xAA, 0x00, 0xBB, 0x00, 0xCC, 0xDD, 0x00, 0xEE,
                                   0xFF, 0x00, 0x12, 0x34, 0x56, 0x00, 0x78, 0x9A};
    const uint8_t bountyRaw[16] = {0x11, 0x22, 0x00, 0x33, 0x44, 0x00, 0x55, 0x66,
                                   0x00, 0x77, 0x88, 0x00, 0x99, 0xAA, 0xBB, 0x00};
    const uint8_t hTeamRaw[16] = {0x01, 0x00, 0x02, 0x03, 0x04, 0x00, 0x05, 0x06,
                                  0x07, 0x08, 0x00, 0x09, 0x0A, 0x0B, 0x0C, 0x0D};
    memcpy(matchUuid.bytes, matchRaw, 16);
    memcpy(hunterPlayerId.bytes, hunterRaw, 16);
    memcpy(bountyPlayerId.bytes, bountyRaw, 16);
    memcpy(hunterTeamId.bytes, hTeamRaw, 16);
    winnerPlayerId = hunterPlayerId;

    char matchIdText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
    matchUuid.format(matchIdText);
    Match original(matchIdText);
    original.setHunterPlayerId(hunterPlayerId);
    original.setBountyPlayerId(bountyPlayerId);
    original.setHunterTeamId(hunterTeamId);
    original.setBountyTeamId(bountyTeamId);  // nil team (unassigned)
    original.setWinnerPlayerId(winnerPlayerId);
    original.setHunterDrawTime(150);
    original.setBountyDrawTime(0);
    original.setOccurredAtUnixMs(1775500000123LL);

    uint8_t buffer[MATCH_BINARY_SIZE] = {};
    ASSERT_EQ(original.serialize(buffer), MATCH_BINARY_SIZE);

    Match restored;
    ASSERT_EQ(restored.deserialize(buffer), MATCH_BINARY_SIZE);

    EXPECT_EQ(restored.getMatchUuid(), matchUuid);
    EXPECT_EQ(restored.getHunterPlayerId(), hunterPlayerId);
    EXPECT_EQ(restored.getBountyPlayerId(), bountyPlayerId);
    EXPECT_EQ(restored.getHunterTeamId(), hunterTeamId);
    EXPECT_TRUE(restored.getBountyTeamId().isZero());
    EXPECT_EQ(restored.getWinnerPlayerId(), winnerPlayerId);
    EXPECT_EQ(restored.getHunterDrawTime(), 150u);
    EXPECT_EQ(restored.getBountyDrawTime(), 0u);
    EXPECT_EQ(restored.getOccurredAtUnixMs(), 1775500000123LL);
}

// ============================================
// Draw Time & Winner Tests
// ============================================

inline void matchSetupClearsDrawTimes() {
    Match match;
    match.setHunterDrawTime(500);
    match.setBountyDrawTime(600);

    // Re-creating the match should reset draw times
    match = Match("new-match");

    EXPECT_EQ(match.getHunterDrawTime(), 0);
    EXPECT_EQ(match.getBountyDrawTime(), 0);
    EXPECT_STREQ(match.getMatchId(), "new-match");
}

inline void matchDrawTimesSetCorrectly() {
    Match match("match");

    match.setHunterDrawTime(123);
    EXPECT_EQ(match.getHunterDrawTime(), 123);

    match.setBountyDrawTime(456);
    EXPECT_EQ(match.getBountyDrawTime(), 456);
}

// ============================================
// Edge Cases
// ============================================

inline void matchWithZeroDrawTimes() {
    Match match("match");

    // Both times at 0 - edge case for tie
    EXPECT_EQ(match.getHunterDrawTime(), 0);
    EXPECT_EQ(match.getBountyDrawTime(), 0);
}

inline void matchWithLargeDrawTimes() {
    Match match("match");

    // Test with large values
    unsigned long largeTime = 999999999UL;
    match.setHunterDrawTime(largeTime);
    match.setBountyDrawTime(largeTime - 1);

    EXPECT_EQ(match.getHunterDrawTime(), largeTime);
    EXPECT_EQ(match.getBountyDrawTime(), largeTime - 1);

    uint8_t buffer[MATCH_BINARY_SIZE] = {};
    match.serialize(buffer);
    Match restored;
    restored.deserialize(buffer);
    EXPECT_EQ(restored.getHunterDrawTime(), largeTime);
}
