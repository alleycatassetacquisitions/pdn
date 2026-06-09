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

inline void matchJsonRoundTripPreservesAllFields() {
    Match original("match-id-12345678-1234-1234-1234-123456789abc", 
                   "hunter-id-12345678-1234-1234-1234-123456789abc", 
                   "bounty-id-12345678-1234-1234-1234-123456789abc");
    original.setHunterDrawTime(250);
    original.setBountyDrawTime(300);

    std::string json = original.toJson();

    Match restored;
    restored.fromJson(json);

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
    
    EXPECT_NE(json.find("\"winner_is_hunter\":true"), std::string::npos);

    // Bounty wins (faster draw time)
    Match bountyWins("match-2", "hunter-2", "bounty-2");
    bountyWins.setHunterDrawTime(350);
    bountyWins.setBountyDrawTime(200);

    json = bountyWins.toJson();
    
    EXPECT_NE(json.find("\"winner_is_hunter\":false"), std::string::npos);
}


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

