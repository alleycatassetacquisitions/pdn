#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "id-generator.hpp"
#include "utility-tests.hpp"

using ::testing::NiceMock;

// ============================================
// Integration Test Fixture
// ============================================

/**
 * Two-device duel simulation. Each device has its own MatchManager and
 * FakeQuickdrawWirelessManager. performHandshake() drives the production
 * SEND_MATCH_ID → MATCH_ID_ACK exchange so tests never call createMatch().
 */
class DuelIntegrationTestSuite : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000);

        hunter = new Player();
        char hunterId[] = "hunt";
        hunter->setUserID(hunterId);
        hunter->setIsHunter(true);

        bounty = new Player();
        char bountyId[] = "boun";
        bounty->setUserID(bountyId);
        bounty->setIsHunter(false);

        hunterWirelessManager = new FakeQuickdrawWirelessManager();
        hunterWirelessManager->initialize(hunter, nullptr, 0);
        hunterMatchManager = new MatchManager();
        hunterMatchManager->initialize(hunter, &hunterStorage, hunterWirelessManager);
        hunterWirelessManager->setPacketReceivedCallback(
            std::bind(&MatchManager::listenForMatchEvents, hunterMatchManager, std::placeholders::_1));

        bountyWirelessManager = new FakeQuickdrawWirelessManager();
        bountyWirelessManager->initialize(bounty, nullptr, 0);
        bountyMatchManager = new MatchManager();
        bountyMatchManager->initialize(bounty, &bountyStorage, bountyWirelessManager);
        bountyWirelessManager->setPacketReceivedCallback(
            std::bind(&MatchManager::listenForMatchEvents, bountyMatchManager, std::placeholders::_1));
    }

    void TearDown() override {
        hunterMatchManager->clearCurrentMatch();
        bountyMatchManager->clearCurrentMatch();
        delete hunterMatchManager;
        delete bountyMatchManager;
        delete hunterWirelessManager;
        delete bountyWirelessManager;
        delete hunter;
        delete bounty;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Runs the full SEND_MATCH_ID → MATCH_ID_ACK handshake so both managers
    // have an active, ready match after this call returns.
    void performHandshake() {
        uint8_t hunterMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        hunterMatchManager->initializeMatch(bountyMac);
        hunterWirelessManager->deliverLastTo(bountyWirelessManager, hunterMac);
        bountyWirelessManager->deliverLastTo(hunterWirelessManager, bountyMac);
    }

    FakePlatformClock* fakeClock = nullptr;
    Player* hunter = nullptr;
    Player* bounty = nullptr;
    MatchManager* hunterMatchManager = nullptr;
    MatchManager* bountyMatchManager = nullptr;
    FakeQuickdrawWirelessManager* hunterWirelessManager = nullptr;
    FakeQuickdrawWirelessManager* bountyWirelessManager = nullptr;
    NiceMock<MockStorage> hunterStorage;
    NiceMock<MockStorage> bountyStorage;
};

// ============================================
// Complete Duel Flow: Two Device Simulation - Hunter Wins
// ============================================

inline void completeDuelFlowHunterWins(DuelIntegrationTestSuite* suite) {
    const unsigned long HUNTER_REACTION_MS = 200;
    const unsigned long BOUNTY_REACTION_MS = 300;

    suite->performHandshake();
    ASSERT_TRUE(suite->hunterMatchManager->getCurrentMatch().has_value());
    ASSERT_TRUE(suite->bountyMatchManager->getCurrentMatch().has_value());

    suite->hunterMatchManager->setDuelLocalStartTime(10000);
    suite->bountyMatchManager->setDuelLocalStartTime(10000);

    // Hunter side
    suite->hunterMatchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->hunterMatchManager->setReceivedButtonPush();
    suite->hunterMatchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->hunterMatchManager->setReceivedDrawResult();

    EXPECT_TRUE(suite->hunterMatchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->hunterMatchManager->didWin());

    suite->hunter->incrementWins();
    suite->hunter->incrementMatchesPlayed();
    suite->hunter->incrementStreak();
    suite->hunter->addReactionTime(HUNTER_REACTION_MS);

    EXPECT_EQ(suite->hunter->getWins(), 1);
    EXPECT_EQ(suite->hunter->getStreak(), 1);

    // Bounty side
    suite->bountyMatchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->bountyMatchManager->setReceivedButtonPush();
    suite->bountyMatchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->bountyMatchManager->setReceivedDrawResult();

    EXPECT_TRUE(suite->bountyMatchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->bountyMatchManager->didWin());

    suite->bounty->incrementLosses();
    suite->bounty->incrementMatchesPlayed();
    suite->bounty->resetStreak();
    suite->bounty->addReactionTime(BOUNTY_REACTION_MS);

    EXPECT_EQ(suite->bounty->getLosses(), 1);
    EXPECT_EQ(suite->bounty->getStreak(), 0);
}

// ============================================
// Complete Duel Flow: Two Device Simulation - Bounty Wins
// ============================================

inline void completeDuelFlowBountyWins(DuelIntegrationTestSuite* suite) {
    const unsigned long HUNTER_REACTION_MS = 350;
    const unsigned long BOUNTY_REACTION_MS = 180;

    suite->performHandshake();

    suite->hunterMatchManager->setDuelLocalStartTime(5000);
    suite->bountyMatchManager->setDuelLocalStartTime(5000);

    // Hunter side
    suite->hunterMatchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->hunterMatchManager->setReceivedButtonPush();
    suite->hunterMatchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->hunterMatchManager->setReceivedDrawResult();

    EXPECT_TRUE(suite->hunterMatchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->hunterMatchManager->didWin());

    // Bounty side
    suite->bountyMatchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->bountyMatchManager->setReceivedButtonPush();
    suite->bountyMatchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->bountyMatchManager->setReceivedDrawResult();

    EXPECT_TRUE(suite->bountyMatchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->bountyMatchManager->didWin());
}

// ============================================
// Match Data Serialization Flow
// ============================================

inline void matchSerializationRoundTrip() {
    Match originalMatch(
        "abcdef12-3456-7890-abcd-ef1234567890",
        "a0b1c2d3-0000-0000-0000-000000000001",
        true
    );
    originalMatch.setHunterDrawTime(225);
    originalMatch.setBountyDrawTime(310);

    uint8_t buffer[MATCH_BINARY_SIZE];
    size_t bytesWritten = originalMatch.serialize(buffer);
    EXPECT_EQ(bytesWritten, MATCH_BINARY_SIZE);

    Match receivedMatch;
    size_t bytesRead = receivedMatch.deserialize(buffer);
    EXPECT_EQ(bytesRead, MATCH_BINARY_SIZE);

    EXPECT_STREQ(receivedMatch.getMatchId(), originalMatch.getMatchId());
    EXPECT_EQ(receivedMatch.getHunterDrawTime(), 225);
    EXPECT_EQ(receivedMatch.getBountyDrawTime(), 310);

    std::string json = receivedMatch.toJson();
    Match jsonRestored;
    jsonRestored.fromJson(json);

    EXPECT_STREQ(jsonRestored.getMatchId(), originalMatch.getMatchId());
    EXPECT_EQ(jsonRestored.getHunterDrawTime(), 225);
}

// ============================================
// Player Stats Flow Over Multiple Matches
// ============================================

inline void playerStatsAccumulateAcrossMatches(Player* player) {
    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();
    player->addReactionTime(200);

    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();
    player->addReactionTime(180);

    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();
    player->addReactionTime(220);

    EXPECT_EQ(player->getStreak(), 3);
    EXPECT_EQ(player->getWins(), 3);

    player->incrementLosses();
    player->incrementMatchesPlayed();
    player->resetStreak();
    player->addReactionTime(350);

    EXPECT_EQ(player->getStreak(), 0);
    EXPECT_EQ(player->getLosses(), 1);

    player->incrementWins();
    player->incrementMatchesPlayed();
    player->incrementStreak();
    player->addReactionTime(190);

    EXPECT_EQ(player->getWins(), 4);
    EXPECT_EQ(player->getLosses(), 1);
    EXPECT_EQ(player->getMatchesPlayed(), 5);
    EXPECT_EQ(player->getStreak(), 1);
    EXPECT_EQ(player->getLastReactionTime(), 190);
    EXPECT_EQ(player->getAverageReactionTime(), 228);
}

// ============================================
// Edge Cases
// ============================================

inline void duelWithTiedReactionTimes(DuelIntegrationTestSuite* suite) {
    suite->performHandshake();

    suite->hunterMatchManager->setHunterDrawTime(250);
    suite->hunterMatchManager->setBountyDrawTime(250);
    suite->hunterMatchManager->setReceivedButtonPush();
    suite->hunterMatchManager->setReceivedDrawResult();

    // Equal times: hunter_time < bounty_time is false, so hunter loses
    EXPECT_FALSE(suite->hunterMatchManager->didWin());
}

inline void duelWithOpponentTimeout(DuelIntegrationTestSuite* suite) {
    suite->performHandshake();

    suite->hunterMatchManager->setHunterDrawTime(300);
    suite->hunterMatchManager->setReceivedButtonPush();
    // Opponent (bounty) sends NEVER_PRESSED
    uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    QuickdrawCommand neverPressed(bountyMac, QDCommand::NEVER_PRESSED,
        suite->hunterMatchManager->getCurrentMatch()->getMatchId(), "boun", 5000, false);
    suite->hunterMatchManager->listenForMatchEvents(neverPressed);

    EXPECT_TRUE(suite->hunterMatchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->hunterMatchManager->didWin());
}
