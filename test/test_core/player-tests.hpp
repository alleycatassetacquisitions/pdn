#pragma once

#include <gtest/gtest.h>
#include "game/player.hpp"
#include "../test-constants.hpp"

class PlayerTestSuite : public testing::Test {
protected:
    void SetUp() override {
        player = new Player();
    }

    void TearDown() override {
        delete player;
    }

    Player* player;
};

// ============================================
// JSON Serialization Tests
// ============================================

inline void playerJsonRoundTripPreservesAllFields(Player* player) {
    // Set up player with all fields populated
    player->setUserID(const_cast<char*>(TestConstants::TEST_UUID_PLAYER_1));
    player->setName("TestPlayer");
    player->setAllegiance(Allegiance::HELIX);
    player->setFaction("TestFaction");
    player->setIsHunter(true);

    // Serialize to JSON
    std::string json = player->toJson();

    // Create new player and deserialize
    Player restored;
    restored.fromJson(json);

    // Verify all fields preserved
    EXPECT_EQ(restored.getUserID(), TestConstants::TEST_UUID_PLAYER_1);
    EXPECT_EQ(restored.getName(), "TestPlayer");
    EXPECT_EQ(restored.getFaction(), "TestFaction");
    EXPECT_TRUE(restored.isHunter());
}

inline void playerJsonRoundTripWithBountyRole(Player* player) {
    player->setUserID(const_cast<char*>(TestConstants::TEST_UUID_BOUNTY_1));
    player->setIsHunter(false);

    std::string json = player->toJson();

    Player restored;
    restored.fromJson(json);

    EXPECT_EQ(restored.getUserID(), TestConstants::TEST_UUID_BOUNTY_1);
    EXPECT_FALSE(restored.isHunter());
}

// ============================================
// Stats Tracking Tests
// ============================================

inline void playerStatsIncrementCorrectly(Player* player) {
    EXPECT_EQ(player->getWins(), 0);
    EXPECT_EQ(player->getLosses(), 0);
    EXPECT_EQ(player->getMatchesPlayed(), 0);
    EXPECT_EQ(player->getStreak(), 0);

    // Simulate winning 3 matches
    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();

    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();

    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();

    EXPECT_EQ(player->getWins(), 3);
    EXPECT_EQ(player->getLosses(), 0);
    EXPECT_EQ(player->getMatchesPlayed(), 3);
    EXPECT_EQ(player->getStreak(), 3);

    // Simulate losing a match (resets streak)
    player->incrementLosses();
    player->incrementMatchesPlayed();
    player->resetStreak();

    EXPECT_EQ(player->getWins(), 3);
    EXPECT_EQ(player->getLosses(), 1);
    EXPECT_EQ(player->getMatchesPlayed(), 4);
    EXPECT_EQ(player->getStreak(), 0);
}

inline void playerStreakResetsOnLoss(Player* player) {
    // Build up a streak
    for (int i = 0; i < 5; i++) {
        player->incrementStreak();
    }
    EXPECT_EQ(player->getStreak(), 5);

    // Reset streak (simulating a loss)
    player->resetStreak();
    EXPECT_EQ(player->getStreak(), 0);
}

// ============================================
// Allegiance Conversion Tests
// ============================================

inline void playerAllegianceFromIntSetsCorrectly(Player* player) {
    player->setAllegiance(0);
    EXPECT_EQ(player->getAllegiance(), Allegiance::ALLEYCAT);
    EXPECT_EQ(player->getAllegianceString(), "None");

    player->setAllegiance(1);
    EXPECT_EQ(player->getAllegiance(), Allegiance::ENDLINE);
    EXPECT_EQ(player->getAllegianceString(), "Endline");

    player->setAllegiance(2);
    EXPECT_EQ(player->getAllegiance(), Allegiance::HELIX);
    EXPECT_EQ(player->getAllegianceString(), "Helix");

    player->setAllegiance(3);
    EXPECT_EQ(player->getAllegiance(), Allegiance::RESISTANCE);
    EXPECT_EQ(player->getAllegianceString(), "The Resistance");
}

inline void playerAllegianceFromStringSetsCorrectly(Player* player) {
    player->setAllegiance("Endline");
    EXPECT_EQ(player->getAllegiance(), Allegiance::ENDLINE);

    player->setAllegiance("Helix");
    EXPECT_EQ(player->getAllegiance(), Allegiance::HELIX);

    player->setAllegiance("The Resistance");
    EXPECT_EQ(player->getAllegiance(), Allegiance::RESISTANCE);
}

// ============================================
// Reaction Time Tests
// ============================================

inline void playerReactionTimeAverageCalculatesCorrectly(Player* player) {
    // No matches played - average should be 0
    EXPECT_EQ(player->getAverageReactionTime(), 0);

    // Add reaction times and matches
    player->addReactionTime(200);
    player->incrementMatchesPlayed();
    EXPECT_EQ(player->getLastReactionTime(), 200);
    EXPECT_EQ(player->getAverageReactionTime(), 200);

    player->addReactionTime(300);
    player->incrementMatchesPlayed();
    EXPECT_EQ(player->getLastReactionTime(), 300);
    EXPECT_EQ(player->getAverageReactionTime(), 250); // (200+300)/2

    player->addReactionTime(400);
    player->incrementMatchesPlayed();
    EXPECT_EQ(player->getLastReactionTime(), 400);
    EXPECT_EQ(player->getAverageReactionTime(), 300); // (200+300+400)/3
}
