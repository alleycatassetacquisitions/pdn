#pragma once

#include <cstdint>
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
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), 0u);
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
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), 3u);
    EXPECT_EQ(player->getStreak(), 3);

    // Simulate losing a match (resets streak)
    player->incrementLosses();
    player->incrementMatchesPlayed();
    player->resetStreak();

    EXPECT_EQ(player->getWins(), 3);
    EXPECT_EQ(player->getLosses(), 1);
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), 4u);
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
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 0u);

    // Add reaction times and matches
    player->addReactionTime(200);
    player->incrementMatchesPlayed();
    EXPECT_EQ(player->getLastReactionTime(), 200);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 200u);

    player->addReactionTime(300);
    player->incrementMatchesPlayed();
    EXPECT_EQ(player->getLastReactionTime(), 300);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 250u);  // (200+300)/2

    player->addReactionTime(400);
    player->incrementMatchesPlayed();
    EXPECT_EQ(player->getLastReactionTime(), 400);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 300u);  // (200+300+400)/3
}

// ============================================
// Displayed Totals Tests
// ============================================

// A finished match as the match states leave it on the session player.
inline void recordSessionMatch(Player* player, bool won, unsigned long reactionTime) {
    player->addReactionTime(reactionTime);
    player->incrementMatchesPlayed();
    if (won) {
        player->incrementWins();
        player->incrementStreak();
        return;
    }
    player->resetStreak();
    player->incrementLosses();
}

inline void registerWithTotals(Player* player, uint32_t matchesPlayed, uint32_t wins,
                               uint32_t averageDrawTimeMs) {
    PlayerTotals totals;
    totals.matchesPlayed = matchesPlayed;
    totals.wins = wins;
    totals.averageDrawTimeMs = averageDrawTimeMs;
    player->applyRegistrationProfile(UUIDBytes(), "Ada", Team(), {}, totals);
}

// A server that sends no totals leaves the screens reading this PDN's own
// counts, as they did before there was a baseline.
inline void playerDisplayedTotalsWithoutServerTotalsAreTheSessionCounts(Player* player) {
    player->applyRegistrationProfile(UUIDBytes(), "Ada", Team(), {});
    recordSessionMatch(player, true, 200);
    recordSessionMatch(player, false, 300);
    recordSessionMatch(player, true, 402);

    EXPECT_EQ(player->getDisplayedWins(), 2u);
    EXPECT_EQ(player->getDisplayedLosses(), 1u);
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), 3u);
    // 902 / 3 rounds down.
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 300u);
}

// A player who has just swapped PDNs reads the server's numbers; the server
// sends no losses, so they are the matches that were not wins.
inline void playerDisplayedTotalsBeforeASessionMatchAreTheServerTotals(Player* player) {
    registerWithTotals(player, 12, 7, 300);

    EXPECT_EQ(player->getDisplayedWins(), 7u);
    EXPECT_EQ(player->getDisplayedLosses(), 5u);
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), 12u);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 300u);
}

inline void playerDisplayedTotalsAddTheSessionToTheServerTotals(Player* player) {
    registerWithTotals(player, 12, 7, 300);
    recordSessionMatch(player, true, 200);
    recordSessionMatch(player, false, 250);
    recordSessionMatch(player, true, 210);

    EXPECT_EQ(player->getDisplayedWins(), 9u);
    EXPECT_EQ(player->getDisplayedLosses(), 6u);
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), 15u);
    // (300 * 12 + 660) / 15: the server's twelve matches outweigh this
    // session's three. The mean of the two averages, 300 and 220, is 260.
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 284u);
}

inline void playerDisplayedAverageIsZeroWithNoMatchesOnEitherSide(Player* player) {
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 0u);

    // An average over no matches has nothing behind it.
    registerWithTotals(player, 0, 0, 345);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 0u);

    // A press is timed before its match is counted, and a match abandoned
    // after the press is never counted.
    player->addReactionTime(180);
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), 0u);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), 0u);
}

// The totals are whatever 32-bit numbers the profile carried, so the sums
// and the product behind the average must not wrap at that width.
inline void playerDisplayedTotalsHoldAtThe32BitLimits(Player* player) {
    const uint64_t limit = UINT32_MAX;
    registerWithTotals(player, UINT32_MAX, UINT32_MAX, UINT32_MAX);
    EXPECT_EQ(player->getDisplayedWins(), limit);
    EXPECT_EQ(player->getDisplayedLosses(), 0u);
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), limit);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), limit);

    // limit * limit + limit is the most the sum of reaction times can reach
    // where the session's own sum is 32 bits wide; over limit + 1 matches it
    // is limit again.
    recordSessionMatch(player, true, UINT32_MAX);
    EXPECT_EQ(player->getDisplayedWins(), limit + 1);
    EXPECT_EQ(player->getDisplayedLosses(), 0u);
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), limit + 1);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), limit);

    Player winless;
    registerWithTotals(&winless, UINT32_MAX, 0, 0);
    recordSessionMatch(&winless, false, 0);
    EXPECT_EQ(winless.getDisplayedLosses(), limit + 1);
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
