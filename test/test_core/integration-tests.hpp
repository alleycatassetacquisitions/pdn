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
 * Two-device duel simulation. Each device has its own MatchManager over its
 * own QuickdrawTestStack. performHandshake() drives the production
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

        hunterMatchManager = new MatchManager();
        hunterMatchManager->initialize(hunter, &hunterStorage, &hunterWireless.transport);
        hunterMatchManager->setRemoteDeviceCoordinator(&hunterFakeRdc);

        bountyMatchManager = new MatchManager();
        bountyMatchManager->initialize(bounty, &bountyStorage, &bountyWireless.transport);
        bountyMatchManager->setRemoteDeviceCoordinator(&bountyFakeRdc);
    }

    void TearDown() override {
        hunterMatchManager->clearCurrentMatch();
        bountyMatchManager->clearCurrentMatch();
        delete hunterMatchManager;
        delete bountyMatchManager;
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
        hunterFakeRdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, bountyMac);
        bountyFakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, hunterMac);
        hunterMatchManager->initializeMatch(bountyMac);
        hunterWireless.deliverLastTo(&bountyWireless.transport, hunterMac);
        bountyWireless.deliverLastTo(&hunterWireless.transport, bountyMac);
    }

    FakePlatformClock* fakeClock = nullptr;
    Player* hunter = nullptr;
    Player* bounty = nullptr;
    MatchManager* hunterMatchManager = nullptr;
    MatchManager* bountyMatchManager = nullptr;
    QuickdrawTestStack hunterWireless;
    QuickdrawTestStack bountyWireless;
    NiceMock<MockStorage> hunterStorage;
    NiceMock<MockStorage> bountyStorage;
    FakeRemoteDeviceCoordinator hunterFakeRdc;
    FakeRemoteDeviceCoordinator bountyFakeRdc;
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

    uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    uint8_t hunterMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    // Hunter side
    suite->hunterMatchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->hunterMatchManager->setReceivedButtonPush();
    suite->hunterMatchManager->listenForMatchEvents(bountyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->hunterMatchManager->getCurrentMatch()->getMatchId(),
        "boun", BOUNTY_REACTION_MS, false));

    EXPECT_TRUE(suite->hunterMatchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->hunterMatchManager->didWin());

    // Bounty side
    suite->bountyMatchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->bountyMatchManager->setReceivedButtonPush();
    suite->bountyMatchManager->listenForMatchEvents(hunterMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->bountyMatchManager->getCurrentMatch()->getMatchId(),
        "hunt", HUNTER_REACTION_MS, true));

    EXPECT_TRUE(suite->bountyMatchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->bountyMatchManager->didWin());
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

    uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    uint8_t hunterMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    // Hunter side
    suite->hunterMatchManager->setHunterDrawTime(HUNTER_REACTION_MS);
    suite->hunterMatchManager->setReceivedButtonPush();
    suite->hunterMatchManager->listenForMatchEvents(bountyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->hunterMatchManager->getCurrentMatch()->getMatchId(),
        "boun", BOUNTY_REACTION_MS, false));

    EXPECT_TRUE(suite->hunterMatchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->hunterMatchManager->didWin());

    // Bounty side
    suite->bountyMatchManager->setBountyDrawTime(BOUNTY_REACTION_MS);
    suite->bountyMatchManager->setReceivedButtonPush();
    suite->bountyMatchManager->listenForMatchEvents(hunterMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->bountyMatchManager->getCurrentMatch()->getMatchId(),
        "hunt", HUNTER_REACTION_MS, true));

    EXPECT_TRUE(suite->bountyMatchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->bountyMatchManager->didWin());
}



// ============================================
// Edge Cases
// ============================================

inline void duelWithTiedReactionTimes(DuelIntegrationTestSuite* suite) {
    suite->performHandshake();

    uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    suite->hunterMatchManager->setHunterDrawTime(250);
    suite->hunterMatchManager->setReceivedButtonPush();
    suite->hunterMatchManager->listenForMatchEvents(bountyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, suite->hunterMatchManager->getCurrentMatch()->getMatchId(),
        "boun", 250, false));

    // Equal times: hunter_time < bounty_time is false, so hunter loses
    EXPECT_FALSE(suite->hunterMatchManager->didWin());
}

inline void duelWithOpponentTimeout(DuelIntegrationTestSuite* suite) {
    suite->performHandshake();

    uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    suite->hunterMatchManager->setHunterDrawTime(300);
    suite->hunterMatchManager->setReceivedButtonPush();
    suite->hunterMatchManager->listenForMatchEvents(bountyMac, makeQuickdrawPacket(QDCommand::NEVER_PRESSED, suite->hunterMatchManager->getCurrentMatch()->getMatchId(),
        "boun", 0, false));

    EXPECT_TRUE(suite->hunterMatchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->hunterMatchManager->didWin());
}
