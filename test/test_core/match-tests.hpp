#pragma once

#include <gtest/gtest.h>
#include "game/match.hpp"
#include "id-generator.hpp"
#include "../test-constants.hpp"

class MatchTestSuite : public testing::Test {
protected:
    void SetUp() override {
    }
};

// ============================================
// JSON Serialization Tests
// ============================================

inline void matchJsonRoundTripPreservesAllFields() {
    // Create a match with all fields
    Match original(TestConstants::TEST_UUID_MATCH_1,
                   TestConstants::TEST_UUID_HUNTER_1,
                   TestConstants::TEST_UUID_BOUNTY_1);
    original.setHunterDrawTime(250);
    original.setBountyDrawTime(300);

    // Serialize to JSON
    std::string json = original.toJson();

    // Deserialize into new match
    Match restored;
    restored.fromJson(json);

    // Verify all fields preserved
    EXPECT_EQ(restored.getMatchId(), original.getMatchId());
    EXPECT_EQ(restored.getHunterId(), original.getHunterId());
    EXPECT_EQ(restored.getBountyId(), original.getBountyId());
    EXPECT_EQ(restored.getHunterDrawTime(), 250);
    EXPECT_EQ(restored.getBountyDrawTime(), 300);
}

inline void matchJsonContainsWinnerFlag() {
    // Hunter wins (faster draw time)
    Match hunterWins(TestConstants::TEST_MATCH_ID_1, TestConstants::TEST_ID_HUNTER, TestConstants::TEST_ID_BOUNTY);
    hunterWins.setHunterDrawTime(200);
    hunterWins.setBountyDrawTime(300);

    std::string json = hunterWins.toJson();
    
    // The JSON should indicate hunter won
    EXPECT_NE(json.find("\"winner_is_hunter\":true"), std::string::npos);

    // Bounty wins (faster draw time)
    Match bountyWins(TestConstants::TEST_MATCH_ID_2, TestConstants::TEST_UUID_HUNTER_2, TestConstants::TEST_UUID_BOUNTY_2);
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
    Match original(TestConstants::TEST_UUID_PLAYER_1,
                   TestConstants::TEST_UUID_BOUNTY_2,
                   TestConstants::TEST_UUID_HUNTER_2);
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
    EXPECT_EQ(restored.getMatchId(), original.getMatchId());
    EXPECT_EQ(restored.getHunterId(), original.getHunterId());
    EXPECT_EQ(restored.getBountyId(), original.getBountyId());
    EXPECT_EQ(restored.getHunterDrawTime(), 150);
    EXPECT_EQ(restored.getBountyDrawTime(), 275);
}

inline void matchBinarySizeIsCorrect() {
    // Verify the binary size constant matches expected
    // 3 UUIDs (16 bytes each) + 2 unsigned longs
    size_t expectedSize = (3 * 16) + (2 * sizeof(unsigned long));
    EXPECT_EQ(Match::binarySize(), expectedSize);
}

// ============================================
// Draw Time & Winner Tests
// ============================================

inline void matchSetupClearsDrawTimes() {
    Match match;
    match.setHunterDrawTime(500);
    match.setBountyDrawTime(600);

    // Setup should reset draw times
    match.setupMatch("new-match", "new-hunter", "new-bounty");

    EXPECT_EQ(match.getHunterDrawTime(), 0);
    EXPECT_EQ(match.getBountyDrawTime(), 0);
    EXPECT_EQ(match.getMatchId(), "new-match");
}

inline void matchDrawTimesSetCorrectly() {
    Match match("match", "hunter", "bounty");

    match.setHunterDrawTime(123);
    EXPECT_EQ(match.getHunterDrawTime(), 123);

    match.setBountyDrawTime(456);
    EXPECT_EQ(match.getBountyDrawTime(), 456);
}

// ============================================
// Edge Cases
// ============================================

inline void matchWithZeroDrawTimes() {
    Match match("match", "hunter", "bounty");
    
    // Both times at 0 - edge case for tie
    EXPECT_EQ(match.getHunterDrawTime(), 0);
    EXPECT_EQ(match.getBountyDrawTime(), 0);

    std::string json = match.toJson();
    
    // With equal times (0 == 0), hunter_time < bounty_time is false
    EXPECT_NE(json.find("\"winner_is_hunter\":false"), std::string::npos);
}

inline void matchWithLargeDrawTimes() {
    Match match("match", "hunter", "bounty");
    
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
