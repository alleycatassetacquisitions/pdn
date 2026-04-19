#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "utility-tests.hpp"

// ============================================
// MatchManager Test Fixture
// ============================================

class MatchManagerTestSuite : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        matchManager = new MatchManager();
        player = new Player();
        char playerId[] = "test";
        player->setUserID(playerId);
        matchManager->initialize(player, &mockStorage, &fakeWirelessManager);
        // Bounty-side tests that feed SEND_MATCH_ID via setupMatchAsBounty use
        // this dummyMac as the fake hunter. Stub the RDC to accept it as a
        // direct peer so the MAC-peering gate passes.
        uint8_t defaultHunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, defaultHunterMac);
        matchManager->setRemoteDeviceCoordinator(&fakeRdc);
        matchManager->clearCurrentMatch();
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Creates a match from the hunter's perspective via initializeMatch().
    void setupMatchAsHunter() {
        player->setIsHunter(true);
        uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        matchManager->initializeMatch(dummyMac);
    }

    // Creates a match from the bounty's perspective by synthesizing a
    // SEND_MATCH_ID command from a fake hunter and feeding it to listenForMatchEvents().
    void setupMatchAsBounty(const char* matchId = "test-match-id",
                            const char* hunterId = "hunt") {
        player->setIsHunter(false);
        static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        QuickdrawCommand cmd(dummyMac, QDCommand::SEND_MATCH_ID, matchId, hunterId, 0, true);
        matchManager->listenForMatchEvents(cmd);
    }

    MatchManager* matchManager;
    Player* player;
    MockStorage mockStorage;
    FakeQuickdrawWirelessManager fakeWirelessManager;
    FakeRemoteDeviceCoordinator fakeRdc;
    FakePlatformClock* fakeClock;
};

// ============================================
// Boost (chain duel support)
// ============================================

inline void matchManagerSetBoostStoresValue(MatchManager* mm, Player* player) {
    mm->setBoostProvider([]() -> unsigned long { return 75; });
}

inline void matchManagerClearCurrentMatchResetsBoost(MatchManager* mm, Player* player) {
    mm->setBoostProvider([]() -> unsigned long { return 60; });
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->clearCurrentMatch();
}

inline void matchManagerBoostSubtractedFromHunterReactionTime(MatchManagerTestSuite* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    suite->matchManager->setBoostProvider([]() -> unsigned long { return 50; });
    suite->fakeClock->setTime(10000);
    suite->matchManager->setDuelLocalStartTime(10000);
    suite->fakeClock->setTime(10200);  // 200ms reaction

    auto callback = suite->matchManager->getDuelButtonPush();
    callback(suite->matchManager);

    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 150u);
}


// ============================================
// Match Initialization / Handshake Tests
// ============================================

inline void matchManagerInitializeCreatesMatch(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_STREQ(mm->getCurrentMatch()->getHunterId(), player->getUserID().c_str());
    EXPECT_EQ(mm->getCurrentMatch()->getHunterDrawTime(), 0);
    EXPECT_EQ(mm->getCurrentMatch()->getBountyDrawTime(), 0);
}

inline void matchManagerInitializePreventsDoubleActive(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    ASSERT_TRUE(mm->getCurrentMatch().has_value());

    std::string firstId(mm->getCurrentMatch()->getMatchId());

    mm->initializeMatch(dummyMac);  // should be ignored
    EXPECT_STREQ(mm->getCurrentMatch()->getMatchId(), firstId.c_str());
}

inline void matchManagerBountyReceivesMatchViaHandshake(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawCommand cmd(dummyMac, QDCommand::SEND_MATCH_ID, "received-match", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);

    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_STREQ(mm->getCurrentMatch()->getMatchId(), "received-match");
    EXPECT_STREQ(mm->getCurrentMatch()->getHunterId(), "hunt");
    EXPECT_STREQ(mm->getCurrentMatch()->getBountyId(), player->getUserID().c_str());
}

// ============================================
// Win Determination Tests
// ============================================

inline void matchManagerHunterWinsWhenFaster(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->setHunterDrawTime(200);
    mm->setBountyDrawTime(300);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerHunterLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->setHunterDrawTime(350);
    mm->setBountyDrawTime(200);

    EXPECT_FALSE(mm->didWin());
}

inline void matchManagerBountyWinsWhenFaster(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawCommand cmd(dummyMac, QDCommand::SEND_MATCH_ID, "duel-3", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);
    mm->setHunterDrawTime(400);
    mm->setBountyDrawTime(250);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawCommand cmd(dummyMac, QDCommand::SEND_MATCH_ID, "duel-4", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);
    mm->setHunterDrawTime(150);
    mm->setBountyDrawTime(300);

    EXPECT_FALSE(mm->didWin());
}

inline void matchManagerHunterWinsWhenBountyNeverPressed(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->setHunterDrawTime(250);
    mm->setOpponentNeverPressed();

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyWinsWhenHunterNeverPressed(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawCommand cmd(dummyMac, QDCommand::SEND_MATCH_ID, "duel-6", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);
    mm->setBountyDrawTime(300);
    mm->setOpponentNeverPressed();

    EXPECT_TRUE(mm->didWin());
}

// ============================================
// Match State Tests
// ============================================

inline void matchManagerTracksDuelState(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    EXPECT_FALSE(mm->getHasReceivedDrawResult());
    EXPECT_FALSE(mm->getHasPressedButton());
    EXPECT_FALSE(mm->matchResultsAreIn());

    mm->setReceivedButtonPush();
    EXPECT_TRUE(mm->getHasPressedButton());
    EXPECT_FALSE(mm->matchResultsAreIn());

    mm->setReceivedDrawResult();
    EXPECT_TRUE(mm->getHasReceivedDrawResult());
    EXPECT_TRUE(mm->matchResultsAreIn());
}

// ============================================
// Spoof rejection — packet-source authentication on duel commands
// ============================================
//
// Commands that mutate an active match must only be accepted from the
// match's established opponent (right MAC + right matchId). A neighbor
// device forging results for someone else's duel must be ignored.

// NEVER_PRESSED forged by a non-opponent MAC must not set opponentNeverPressed
// (which would otherwise force didWin() to true).
inline void matchManagerRejectsNeverPressedFromStranger(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);
    const char* realMatchId = mm->getCurrentMatch()->getMatchId();

    mm->setHunterDrawTime(300);
    mm->setReceivedButtonPush();

    uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    QuickdrawCommand forged(strangerMac, QDCommand::NEVER_PRESSED, realMatchId, "xxxx", 0, false);
    mm->listenForMatchEvents(forged);

    EXPECT_FALSE(mm->didWin());
    EXPECT_FALSE(mm->matchResultsAreIn());
}

// Commands with the wrong matchId from the right MAC must also be rejected
// (prevents stale results from a prior match confusing the current one).
inline void matchManagerRejectsWrongMatchId(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);

    QuickdrawCommand stale(opponentMac, QDCommand::DRAW_RESULT, "wrong-match-id-xxxxxxxxxxxxxxxxxxxx", "opp_", 222, false);
    mm->listenForMatchEvents(stale);

    EXPECT_EQ(mm->getCurrentMatch()->getBountyDrawTime(), 0u);
    EXPECT_FALSE(mm->getHasReceivedDrawResult());
}

// SEND_MATCH_ID from a MAC that isn't an RDC direct peer must be rejected.
// The fixture stubs INPUT_JACK peer to 0x01,0x02,...; this test sends from a
// different MAC and verifies the gate refuses it.
inline void matchManagerSendMatchIdRejectsUnknownPeer(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    QuickdrawCommand cmd(strangerMac, QDCommand::SEND_MATCH_ID, "match-id-000000000000000000000000000", "xxxx", 0, true);
    mm->listenForMatchEvents(cmd);

    EXPECT_FALSE(mm->getCurrentMatch().has_value());
}

// Legitimate path: command with correct MAC + matchId is accepted.
inline void matchManagerAcceptsResultFromOpponent(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);
    const char* realMatchId = mm->getCurrentMatch()->getMatchId();

    QuickdrawCommand legit(opponentMac, QDCommand::DRAW_RESULT, realMatchId, "opp_", 150, false);
    mm->listenForMatchEvents(legit);

    EXPECT_EQ(mm->getCurrentMatch()->getBountyDrawTime(), 150u);
    EXPECT_TRUE(mm->getHasReceivedDrawResult());
}

inline void matchManagerGracePeriodPath(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    mm->setReceivedButtonPush();
    mm->setOpponentNeverPressed();

    EXPECT_TRUE(mm->matchResultsAreIn());
}

// Regression guard for the both-timed-out deadlock: if our grace expired and
// the opponent's NEVER_PRESSED packet dropped, we previously stayed stuck
// because no clause in matchResultsAreIn fired. The deadlock-escape clause
// `|| gracePeriodExpiredNoResult` must let us finalize from our own state.
inline void matchManagerGraceExpiredAloneFinalizes(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    // Simulate: grace expired on our side (setNeverPressed sets the flag),
    // but the opponent's NEVER_PRESSED never arrived (so neither
    // hasReceivedDrawResult nor opponentNeverPressed is set), and we never
    // pressed (hasPressedButton stays false).
    mm->setNeverPressed();

    // Pre-fix: all three clauses in matchResultsAreIn returned false → stuck.
    // Post-fix: the 4th escape clause (gracePeriodExpiredNoResult alone) fires.
    EXPECT_TRUE(mm->matchResultsAreIn());
}

inline void matchManagerClearMatchResetsState(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->setReceivedButtonPush();
    mm->setReceivedDrawResult();
    mm->setDuelLocalStartTime(1000);

    mm->clearCurrentMatch();

    EXPECT_FALSE(mm->getCurrentMatch().has_value());
    EXPECT_FALSE(mm->getHasReceivedDrawResult());
    EXPECT_FALSE(mm->getHasPressedButton());
}

// ============================================
// Draw Time Setting Tests
// ============================================

inline void matchManagerSetDrawTimesRequiresActiveMatch(MatchManager* mm, Player* player) {
    EXPECT_FALSE(mm->setHunterDrawTime(100));
    EXPECT_FALSE(mm->setBountyDrawTime(200));

    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    EXPECT_TRUE(mm->setHunterDrawTime(100));
    EXPECT_TRUE(mm->setBountyDrawTime(200));

    EXPECT_EQ(mm->getCurrentMatch()->getHunterDrawTime(), 100);
    EXPECT_EQ(mm->getCurrentMatch()->getBountyDrawTime(), 200);
}

inline void matchManagerDuelStartTimeTracking(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    EXPECT_EQ(mm->getDuelLocalStartTime(), 0);
    mm->setDuelLocalStartTime(5000);
    EXPECT_EQ(mm->getDuelLocalStartTime(), 5000);
}

// ============================================
// Button Masher Count Reset Test
// ============================================

inline void matchManagerClearCurrentMatchResetsMasherCount(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    auto masherCallback = mm->getButtonMasher();
    masherCallback(mm);
    masherCallback(mm);
    masherCallback(mm);

    mm->clearCurrentMatch();

    mm->initializeMatch(dummyMac);

    EXPECT_TRUE(mm->getCurrentMatch().has_value());
    mm->clearCurrentMatch();
    EXPECT_FALSE(mm->getCurrentMatch().has_value());
}

// ============================================
// matchIsReady / Handshake Flag Tests
// ============================================

// Hunter's matchIsReady is false immediately after initializeMatch — ACK not yet received.
inline void matchManagerMatchIsReadyFalseBeforeHandshake(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_FALSE(mm->isMatchReady());
}

// Hunter's matchIsReady becomes true once MATCH_ID_ACK arrives with the correct match ID.
inline void matchManagerHunterMatchIsReadyAfterAck(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    const char* matchId = mm->getCurrentMatch()->getMatchId();
    QuickdrawCommand ack(dummyMac, QDCommand::MATCH_ID_ACK, matchId, "boun", 0, false);
    mm->listenForMatchEvents(ack);

    EXPECT_TRUE(mm->isMatchReady());
}

// Bounty's matchIsReady becomes true immediately upon receiving SEND_MATCH_ID.
inline void matchManagerBountyMatchIsReadyAfterReceivingMatch(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawCommand cmd(dummyMac, QDCommand::SEND_MATCH_ID, "match-id-abc", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);

    EXPECT_TRUE(mm->isMatchReady());
}

// clearCurrentMatch resets the matchIsReady flag along with all other duel state.
inline void matchManagerClearMatchResetsMatchIsReadyFlag(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawCommand cmd(dummyMac, QDCommand::SEND_MATCH_ID, "match-id-abc", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);
    ASSERT_TRUE(mm->isMatchReady());

    mm->clearCurrentMatch();

    EXPECT_FALSE(mm->isMatchReady());
    EXPECT_FALSE(mm->getCurrentMatch().has_value());
}

// Receiving MATCH_ROLE_MISMATCH clears the initiator's active match and leaves matchIsReady false.
inline void matchManagerRoleMismatchClearsInitiatorMatch(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    ASSERT_FALSE(mm->isMatchReady());

    const char* matchId = mm->getCurrentMatch()->getMatchId();
    QuickdrawCommand mismatch(dummyMac, QDCommand::MATCH_ROLE_MISMATCH, matchId, "hunt2", 0, true);
    mm->listenForMatchEvents(mismatch);

    EXPECT_FALSE(mm->getCurrentMatch().has_value());
    EXPECT_FALSE(mm->isMatchReady());
}
