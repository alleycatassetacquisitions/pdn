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

// The role-flip observer is what drives the connection-context re-broadcast, so
// it must fire on every real flip and stay silent on a set that reasserts the
// current role. Every profile applied, and a clear of a held one, fires once.
inline void playerRoleChangeFiresOnFlipAndProfileChange(Player* player) {
    int fires = 0;
    player->setIsHunter(true);
    player->setOnRoleChanged([&fires]() { fires++; });

    player->setIsHunter(true);
    EXPECT_EQ(fires, 0);

    player->setIsHunter(false);
    EXPECT_EQ(fires, 1);
    EXPECT_FALSE(player->isHunter());

    player->toggleHunter();
    EXPECT_EQ(fires, 2);
    EXPECT_TRUE(player->isHunter());

    Team bountyTeam;
    std::snprintf(bountyTeam.name, sizeof(bountyTeam.name), "%s", "Bounty");
    player->applyRegistrationProfile(UUIDBytes{}, "Ada", bountyTeam, {});
    EXPECT_EQ(fires, 3) << "a profile that flips the role fires once";
    EXPECT_FALSE(player->isHunter());
    player->applyRegistrationProfile(UUIDBytes{}, "Ada", bountyTeam, {});
    EXPECT_EQ(fires, 4);

    player->clearRegistrationProfile();
    EXPECT_EQ(fires, 5);
    player->clearRegistrationProfile();
    EXPECT_EQ(fires, 5) << "clearing with no profile held changes nothing";
}

// Only "Hunter" hunts; a team this build does not know, or none, is Bounty.
// "Reboot" is Resistance, and no affiliation is Alleycat.
inline void playerProfileMapsRoleAndAllegiance(Player* player) {
    Team strangeTeam;
    strangeTeam.id.bytes[0] = 1;
    std::snprintf(strangeTeam.name, sizeof(strangeTeam.name), "%s", "Goats");
    Affiliation reboot;
    std::snprintf(reboot.affiliationName, sizeof(reboot.affiliationName), "%s", "Reboot Crew");
    player->setIsHunter(true);
    player->applyRegistrationProfile(UUIDBytes{}, "Ada", strangeTeam, {reboot});
    EXPECT_FALSE(player->isHunter());
    EXPECT_EQ(player->getAllegiance(), Allegiance::RESISTANCE);

    player->setIsHunter(true);
    player->applyRegistrationProfile(UUIDBytes{}, "Ada", Team(), {});
    EXPECT_FALSE(player->isHunter()) << "no team";
    EXPECT_EQ(player->getAllegiance(), Allegiance::ALLEYCAT);
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

    // An FDN queues for registration exactly the PDNs that report no applied profile.
    EXPECT_EQ(player->toProfile().registered, 0);
    player->applyRegistrationProfile(UUIDBytes{}, "Demo", Team(), {});
    EXPECT_EQ(player->toProfile().registered, 1) << "the demo profile counts as applied";
    player->clearRegistrationProfile();
    EXPECT_EQ(player->toProfile().registered, 0);
}

// Test ids must differ per PDN, stay stable per MAC and never look like a v4 server UUID.
inline void testPlayerIdFromMacIsDistinctAndNeverV4() {
    const uint8_t first[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x07};
    const uint8_t second[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x08};
    UUIDBytes firstId = Player::testPlayerIdFromMac(first);

    EXPECT_FALSE(firstId.isZero());
    EXPECT_NE(firstId, Player::testPlayerIdFromMac(second));
    EXPECT_EQ(firstId, Player::testPlayerIdFromMac(first));
    const uint8_t expected[UUIDBytes::SIZE] = {0x07, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
                                               0x7e, 0x57, 0x7e, 0x57, 0x7e, 0x57, 0x7e, 0x57};
    for (size_t index = 0; index < UUIDBytes::SIZE; index++) {
        EXPECT_EQ(firstId.bytes[index], expected[index]) << "byte " << index;
    }
    EXPECT_EQ(firstId.bytes[6] >> 4, 0);
}
