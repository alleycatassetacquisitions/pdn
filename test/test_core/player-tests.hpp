#pragma once

#include <gtest/gtest.h>
#include "game/player.hpp"

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
    char testId[] = "test-uuid-1234";
    player->setUserID(testId);
    player->setName("TestPlayer");
    player->setAllegiance(Allegiance::HELIX);
    player->setFaction("TestFaction");
    player->setIsHunter(true);

    std::string json = player->toJson();

    Player restored;
    restored.fromJson(json);

    EXPECT_EQ(restored.getUserID(), "test-uuid-1234");
    EXPECT_EQ(restored.getName(), "TestPlayer");
    EXPECT_EQ(restored.getFaction(), "TestFaction");
    EXPECT_TRUE(restored.isHunter());
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
