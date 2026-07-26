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
    // Set up player with all fields populated
    char testId[] = "test-uuid-1234";
    player->setUserID(testId);
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
    EXPECT_EQ(restored.getUserID(), "test-uuid-1234");
    EXPECT_EQ(restored.getName(), "TestPlayer");
    EXPECT_EQ(restored.getFaction(), "TestFaction");
    EXPECT_TRUE(restored.isHunter());
}

inline void playerJsonRoundTripWithBountyRole(Player* player) {
    char testId[] = "bounty-player-id";
    player->setUserID(testId);
    player->setIsHunter(false);

    std::string json = player->toJson();

    Player restored;
    restored.fromJson(json);

    EXPECT_EQ(restored.getUserID(), "bounty-player-id");
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

// ============================================
// Role Change + Outgoing Profile Tests
// ============================================

// The role-flip observer is what drives the connection-context re-broadcast, so
// it must fire on every real flip and stay silent on a set that reasserts the
// current role — an idle re-registration would otherwise put a redundant frame
// on the air for every peer.
inline void playerRoleChangeFiresOnlyOnFlip(Player* player) {
    int flips = 0;
    player->setIsHunter(true);
    player->setOnRoleChanged([&flips]() { flips++; });

    player->setIsHunter(true);
    EXPECT_EQ(flips, 0);

    player->setIsHunter(false);
    EXPECT_EQ(flips, 1);
    EXPECT_FALSE(player->isHunter());

    player->toggleHunter();
    EXPECT_EQ(flips, 2);
    EXPECT_TRUE(player->isHunter());

    // A fetched profile is the other role write path and must notify too.
    Player source("1234", Allegiance::HELIX, false);
    player->fromJson(source.toJson());
    EXPECT_EQ(flips, 3);
    EXPECT_FALSE(player->isHunter());
}

// toProfile is what a peer decodes out of the connection context, so gameRole
// must track the live role and an unregistered id must read as the sentinel
// rather than as player 0.
inline void playerProfileCarriesRoleAndIdentity(Player* player) {
    PlayerProfile unregistered = player->toProfile();
    EXPECT_EQ(unregistered.userId, 0xFFFF);

    char testId[] = "4242";
    player->setUserID(testId);
    player->setName("A-very-long-player-name");
    player->setFaction("LongFactionName");
    player->setAllegiance(Allegiance::HELIX);
    player->setIsHunter(true);

    PlayerProfile hunter = player->toProfile();
    EXPECT_EQ(hunter.userId, 4242);
    EXPECT_EQ(hunter.gameRole, 1);
    EXPECT_EQ(hunter.allegiance, static_cast<uint8_t>(Allegiance::HELIX));
    // Truncated to the fixed wire widths and still NUL-terminated.
    EXPECT_EQ(std::string(hunter.name), "A-very-long-pla");
    EXPECT_EQ(std::string(hunter.faction), "LongFac");

    player->setIsHunter(false);
    EXPECT_EQ(player->toProfile().gameRole, 0);
}
