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

// Three string arguments must select the two-player constructor. They used to
// convert the third to bool and build a hunter-only match whose bounty was
// never set, which the existing assertions could not see because they read only
// the winner flag, and that is derived from draw times.
inline void matchThreeStringsStoreBothPlayers() {
    Match match("match-1", "hunt", "bnty");

    EXPECT_STREQ(match.getHunterId(), "hunt");
    EXPECT_STREQ(match.getBountyId(), "bnty")
        << "the third argument was taken as a bool, so no bounty was stored";
}

// An id shorter than its field must not carry the bytes that followed it. The
// string accessors cannot see this — a copy bounded by the destination drags in
// whatever sat after the terminator, and strcmp stops at the terminator anyway.
// serialize() is where it shows: it copies PLAYER_ID_BINARY_SIZE raw bytes, so
// the tail reaches the uploaded record. The source here deliberately has
// non-zero bytes after its terminator, which is what a c_str() into a longer
// buffer looks like.
inline void matchShortIdsDoNotSerializeTrailingBytes() {
    char noisyHunter[8] = {'a', 'b', '\0', 'Z', 'Z', 'Z', 'Z', 'Z'};
    char noisyBounty[8] = {'c', 'd', '\0', 'Y', 'Y', 'Y', 'Y', 'Y'};

    Match match("m", noisyHunter, noisyBounty);

    uint8_t buffer[MATCH_BINARY_SIZE] = {};
    match.serialize(buffer);

    // Layout: UUID_BINARY_SIZE bytes of match id, then hunter, then bounty.
    const uint8_t* hunterBytes = buffer + IdGenerator::UUID_BINARY_SIZE;
    const uint8_t* bountyBytes = hunterBytes + PLAYER_ID_BINARY_SIZE;

    EXPECT_EQ(hunterBytes[3], 0) << "byte after the hunter id came from past its end";
    EXPECT_EQ(bountyBytes[3], 0) << "byte after the bounty id came from past its end";
    EXPECT_STREQ(match.getHunterId(), "ab");
    EXPECT_STREQ(match.getBountyId(), "cd");
}

inline void matchJsonRoundTripPreservesAllFields() {
    // Create a match with all fields
    Match original("match-id-12345678-1234-1234-1234-123456789abc", 
                   "hunter-id-12345678-1234-1234-1234-123456789abc", 
                   "bounty-id-12345678-1234-1234-1234-123456789abc");
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
    Match hunterWins("match-1", "hunter-1", "bounty-1");
    hunterWins.setHunterDrawTime(200);
    hunterWins.setBountyDrawTime(300);

    std::string json = hunterWins.toJson();
    
    // The JSON should indicate hunter won
    EXPECT_NE(json.find("\"winner_is_hunter\":true"), std::string::npos);

    // Bounty wins (faster draw time)
    Match bountyWins("match-2", "hunter-2", "bounty-2");
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
                   "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", 
                   "11111111-2222-3333-4444-555555555555");
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
