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

// The observer is what drives the connection-context re-broadcast, so it must
// fire on every real flip and stay silent on a set that reasserts the current
// role.
inline void playerObserverFiresOnFlipAndAppliedProfile(Player* player) {
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

    // An applied profile notifies once, role flip or not, and only when the
    // whole profile is in place: the observer sends it as found.
    PlayerProfile seen{};
    player->setOnRoleChanged([&flips, &seen, player]() {
        flips++;
        seen = player->toProfile();
    });
    Team hunterTeam;
    std::snprintf(hunterTeam.name, sizeof(hunterTeam.name), "%s", "Hunter");
    Affiliation helix;
    std::snprintf(helix.affiliationName, sizeof(helix.affiliationName), "%s", "Helix");
    player->applyRegistrationProfile(UUIDBytes{}, "Ada", hunterTeam, {helix});
    EXPECT_EQ(flips, 3);
    EXPECT_EQ(seen.allegiance, static_cast<uint8_t>(Allegiance::HELIX));
    EXPECT_EQ(seen.registered, 1);

    player->applyRegistrationProfile(UUIDBytes{}, "Ada", hunterTeam, {helix});
    EXPECT_EQ(flips, 4);
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

// A profile nobody filled reads as unregistered, and "70001" must not wrap into
// a plausible four-digit id (4465).
inline void playerProfileRejectsOutOfRangeUserIds(Player* player) {
    EXPECT_EQ(PlayerProfile{}.userId, PEER_USER_ID_NONE);

    char zeroId[] = "0";
    player->setUserID(zeroId);
    EXPECT_EQ(player->toProfile().userId, 0);

    char maxValidId[] = "65534";
    player->setUserID(maxValidId);
    EXPECT_EQ(player->toProfile().userId, 0xFFFE);

    char sentinelId[] = "65535";
    player->setUserID(sentinelId);
    EXPECT_EQ(player->toProfile().userId, PEER_USER_ID_NONE);

    char overflowId[] = "70001";
    player->setUserID(overflowId);
    EXPECT_EQ(player->toProfile().userId, PEER_USER_ID_NONE);
}
