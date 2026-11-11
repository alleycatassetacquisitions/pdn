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
// JSON Serialization Tests
// ============================================

// Match() is defaulted, so the member initializers are the only thing that
// empties a fresh match. Nothing else in the suite reads that state.
inline void matchDefaultConstructionIsEmpty() {
    Match match;

    EXPECT_STREQ(match.getMatchId(), "");
    EXPECT_STREQ(match.getHunterId(), "");
    EXPECT_STREQ(match.getBountyId(), "");
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

// The wire field is 4 raw bytes with no terminator, so bytes after an early NUL
// are whatever the sender left there. Reading one must not strand it in the field
// — the setters guarantee a cleared tail and deserialize has to agree with them.
inline void matchDeserializeClearsPlayerIdTail() {
    uint8_t buffer[MATCH_BINARY_SIZE] = {};
    uint8_t* hunterField = buffer + IdGenerator::UUID_BINARY_SIZE;
    hunterField[0] = 'a';
    hunterField[1] = 'b';
    hunterField[2] = '\0';
    hunterField[3] = 0x7F;

    Match match;
    match.deserialize(buffer);

    EXPECT_STREQ(match.getHunterId(), "ab");
    EXPECT_EQ(match.getHunterId()[3], '\0') << "stale wire byte survived past the terminator";
}

// Pins copyId's zero-fill: overwriting with a shorter id must not leave the
// previous id's bytes in the tail. EXPECT_STREQ cannot see this, because strcmp
// stops at the terminator. Index 3 discriminates; index 2 holds a terminator
// whether the tail was cleared or not.
inline void matchShorterIdOverwriteClearsTheTail() {
    Match match("m", "abcd", true);
    match.setBountyId("wxyz");

    match.setHunterId("ab");
    match.setBountyId("wx");

    EXPECT_EQ(match.getHunterId()[3], '\0') << "hunter field tail was not cleared";
    EXPECT_EQ(match.getBountyId()[3], '\0') << "bounty field tail was not cleared";
    EXPECT_STREQ(match.getHunterId(), "ab");
    EXPECT_STREQ(match.getBountyId(), "wx");
}

// ============================================
// Binary Serialization Tests
// ============================================

inline void matchBinaryRoundTripPreservesAllFields() {
    // Create a match with valid UUID format strings
    Match original("12345678-1234-1234-1234-123456789abc",
                   "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", true);
    original.setBountyId("11111111-2222-3333-4444-555555555555");
    original.setHunterDrawTime(150);
    original.setBountyDrawTime(275);

    // Serialize to binary
    uint8_t buffer[MATCH_BINARY_SIZE];
    size_t bytesWritten = original.serialize(buffer);

    EXPECT_EQ(bytesWritten, MATCH_BINARY_SIZE);

    // Deserialize into new match
    Match restored;
    size_t bytesRead = restored.deserialize(buffer);

    EXPECT_EQ(bytesRead, MATCH_BINARY_SIZE);

    // Verify all fields preserved
    EXPECT_STREQ(restored.getMatchId(), original.getMatchId());
    EXPECT_STREQ(restored.getHunterId(), original.getHunterId());
    EXPECT_STREQ(restored.getBountyId(), original.getBountyId());
    EXPECT_EQ(restored.getHunterDrawTime(), 150);
    EXPECT_EQ(restored.getBountyDrawTime(), 275);
}

inline void matchBinarySizeIsCorrect() {
    // Fixed so a 64-bit native build and the ESP32 read each other's records.
    EXPECT_EQ(Match::binarySize(), 132u);
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

    const uint8_t hMac[6] = {0xAA, 0x00, 0xBB, 0xCC, 0x00, 0xDD};
    const uint8_t bMac[6] = {0x11, 0x22, 0x00, 0x33, 0x44, 0x55};

    char matchIdText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
    matchUuid.format(matchIdText);
    Match original(matchIdText, "hunt", true);
    original.setHunterPlayerId(hunterPlayerId);
    original.setBountyPlayerId(bountyPlayerId);
    original.setHunterTeamId(hunterTeamId);
    original.setBountyTeamId(bountyTeamId);  // nil team (unassigned)
    original.setWinnerPlayerId(winnerPlayerId);
    original.setHunterMac(hMac);
    original.setBountyMac(bMac);
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
    EXPECT_EQ(memcmp(restored.getHunterMac(), hMac, 6), 0);
    EXPECT_EQ(memcmp(restored.getBountyMac(), bMac, 6), 0);
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
    match = Match("new-match", "new-hunter", true);
    match.setBountyId("new-bounty");

    EXPECT_EQ(match.getHunterDrawTime(), 0);
    EXPECT_EQ(match.getBountyDrawTime(), 0);
    EXPECT_STREQ(match.getMatchId(), "new-match");
}

inline void matchDrawTimesSetCorrectly() {
    Match match("match", "hunter", true);
    match.setBountyId("bounty");

    match.setHunterDrawTime(123);
    EXPECT_EQ(match.getHunterDrawTime(), 123);

    match.setBountyDrawTime(456);
    EXPECT_EQ(match.getBountyDrawTime(), 456);
}

// ============================================
// Edge Cases
// ============================================

inline void matchWithZeroDrawTimes() {
    Match match("match", "hunter", true);
    match.setBountyId("bounty");

    // Both times at 0 - edge case for tie
    EXPECT_EQ(match.getHunterDrawTime(), 0);
    EXPECT_EQ(match.getBountyDrawTime(), 0);
}

inline void matchWithLargeDrawTimes() {
    Match match("match", "hunter", true);
    match.setBountyId("bounty");

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
