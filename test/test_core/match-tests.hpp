#pragma once

#include <gtest/gtest.h>
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

// Shootout ids are "SHT-" then 32 digits: 36 characters carrying one hyphen
// where a UUID carries four. The conversion packs two hex characters per byte
// and skips hyphens, so those three missing hyphens are worth two extra bytes
// past a 16-byte field. The surrounding canary is what a plain native run sees;
// under ASan the real 16-byte case reports directly.
inline void matchShootoutIdConversionStaysInBounds() {
    char shootoutId[IdGenerator::UUID_BUFFER_SIZE];
    snprintf(shootoutId, sizeof(shootoutId), "SHT-%032d", 3);
    ASSERT_EQ(strlen(shootoutId), IdGenerator::UUID_STRING_LENGTH);

    uint8_t bytes[IdGenerator::UUID_BINARY_SIZE + 4];
    memset(bytes, 0xAA, sizeof(bytes));

    IdGenerator::uuidStringToBytes(shootoutId, bytes);

    for (size_t i = IdGenerator::UUID_BINARY_SIZE; i < sizeof(bytes); i++) {
        EXPECT_EQ(bytes[i], 0xAA) << "wrote past the uuid field at index " << i;
    }
}

// A shorter id must not leave the previous one's bytes in the field. STREQ
// cannot see this: strcmp stops at the terminator the same way a bad copy would.
// Index 3 is the discriminating byte — index 2 holds a terminator either way.
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

inline void matchJsonRoundTripPreservesAllFields() {
    // Create a match with all fields
    Match original("match-id-12345678-1234-1234-1234-123456789abc",
                   "hunter-id-12345678-1234-1234-1234-123456789abc", true);
    original.setBountyId("bounty-id-12345678-1234-1234-1234-123456789abc");
    original.setHunterDrawTime(250);
    original.setBountyDrawTime(300);

    // Serialize to JSON
    std::string json = original.toJson();

    // Deserialize into new match
    Match restored;
    restored.fromJson(json);

    // Verify all fields preserved
    EXPECT_STREQ(restored.getMatchId(), original.getMatchId());
    EXPECT_STREQ(restored.getHunterId(), original.getHunterId());
    EXPECT_STREQ(restored.getBountyId(), original.getBountyId());
    EXPECT_EQ(restored.getHunterDrawTime(), 250);
    EXPECT_EQ(restored.getBountyDrawTime(), 300);
}

inline void matchJsonContainsWinnerFlag() {
    // Hunter wins (faster draw time)
    Match hunterWins("match-1", "hunter-1", true);
    hunterWins.setBountyId("bounty-1");
    hunterWins.setHunterDrawTime(200);
    hunterWins.setBountyDrawTime(300);

    std::string json = hunterWins.toJson();
    
    // The JSON should indicate hunter won
    EXPECT_NE(json.find("\"winner_is_hunter\":true"), std::string::npos);

    // Bounty wins (faster draw time)
    Match bountyWins("match-2", "hunter-2", true);
    bountyWins.setBountyId("bounty-2");
    bountyWins.setHunterDrawTime(350);
    bountyWins.setBountyDrawTime(200);

    json = bountyWins.toJson();
    
    // The JSON should indicate bounty won
    EXPECT_NE(json.find("\"winner_is_hunter\":false"), std::string::npos);
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
    // Verify the binary size constant matches expected
    // 1 UUID match_id (16 bytes) + 2 player IDs (4 bytes each) + 2 unsigned longs
    size_t expectedSize = 16 + (2 * PLAYER_ID_BINARY_SIZE) + (2 * sizeof(unsigned long));
    EXPECT_EQ(Match::binarySize(), expectedSize);
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

    std::string json = match.toJson();
    
    // With equal times (0 == 0), hunter_time < bounty_time is false
    EXPECT_NE(json.find("\"winner_is_hunter\":false"), std::string::npos);
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

    // Bounty should win (faster by 1ms)
    std::string json = match.toJson();
    EXPECT_NE(json.find("\"winner_is_hunter\":false"), std::string::npos);
}
