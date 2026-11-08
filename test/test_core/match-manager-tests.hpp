#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "alleycat-server/fdn-match-spool.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
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

        matchManager = new MatchManager(device.wirelessManager);
        player = new Player();
        char playerId[] = "test";
        player->setUserID(playerId);
        matchManager->initialize(player, &mockStorage);
        using ::testing::_;
        ON_CALL(mockStorage, write(_, _, _))
            .WillByDefault([](const std::string&, const std::string&, const std::string& value) {
                return value.size();
            });
        ON_CALL(mockStorage, writeUChar(_, _, _)).WillByDefault(::testing::Return(1));
        ON_CALL(mockStorage, readUChar(_, _, _)).WillByDefault(::testing::Return(0));
        ON_CALL(mockStorage, read(_, _, _)).WillByDefault(::testing::Return(""));
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
        MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, matchId, hunterId, 0, true);
        matchManager->listenForMatchEvents(cmd);
    }

    MatchManager* matchManager;
    Player* player;
    MockDevice device;
    MockStorage mockStorage;
    FakeRemoteDeviceCoordinator fakeRdc;
    FakePlatformClock* fakeClock;
};

// initializeMatch hands primeMatch the generator's own buffer rather than a copy,
// so this pins that the id survives as far as the Match. Nothing else in the suite
// reads the generated id — every other test supplies one.
inline void matchManagerInitializeStoresGeneratedMatchId(MatchManagerTestSuite* suite) {
    suite->setupMatchAsHunter();

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(strlen(suite->matchManager->getCurrentMatch()->getMatchId()),
              IdGenerator::UUID_STRING_LENGTH);
}

// ============================================
// Boost (posse support)
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
    suite->matchManager->setMatchLocalStartTime(10000);
    suite->fakeClock->setTime(10200);  // 200ms reaction

    parameterizedCallbackFunction callback = suite->matchManager->getMatchButtonPush();
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
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "received-match", "hunt", 0, true);
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
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-3", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);
    mm->setHunterDrawTime(400);
    mm->setBountyDrawTime(250);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-4", "hunt", 0, true);
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
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-6", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);
    mm->setBountyDrawTime(300);
    mm->setOpponentNeverPressed();

    EXPECT_TRUE(mm->didWin());
}

// ============================================
// Match State Tests
// ============================================

inline void matchManagerTracksMatchState(MatchManager* mm, Player* player) {
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

// ==========================================================================
// Shootout draw slots: assigned per match by MAC ordering, with the standing role
// left alone, so the two disagree routinely.
// ==========================================================================

// The production sequence across a match boundary: the finished match's MatchState
// dismounts and clears, then the next match is announced. Both halves have to
// land — a slot without a match cannot record a draw time, and the device times
// out into a forfeit whose direction that same slot decides.
inline void eachMatchIsPrimedWithItsOwnDrawSlot(MatchManagerTestSuite* suite) {
    uint8_t opponent[6] = {0x05, 0, 0, 0, 0, 0};
    suite->matchManager->initializeShootoutMatch(
        "SHT-00000000000000000000000000000000", opponent, false);
    ASSERT_FALSE(suite->matchManager->isLocalHunter());

    suite->matchManager->clearCurrentMatch();
    uint8_t nextOpponent[6] = {0x09, 0, 0, 0, 0, 0};
    suite->matchManager->initializeShootoutMatch(
        "SHT-00000000000000000000000000000001", nextOpponent, true);

    EXPECT_TRUE(suite->matchManager->isLocalHunter())
    << "the new match's draw slot was dropped; the next match resolves on the old one";
    EXPECT_TRUE(suite->matchManager->getCurrentMatch().has_value())
        << "the next match was never primed, so a button press in it is discarded";
}

inline void matchManagerShootoutDrawSlotDecidesTheWinner(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeShootoutMatch("SHT-00000000000000000000000000000", opponentMac,
                                /*localIsHunter=*/false);
    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    ASSERT_FALSE(mm->isLocalHunter());

    mm->setBountyDrawTime(400);  // this device
    mm->setHunterDrawTime(100);  // the opponent
    mm->setReceivedButtonPush();
    mm->setReceivedDrawResult();

    EXPECT_FALSE(mm->didWin()) << "the standing role, not the match's draw slot, decided the match";
}

// ============================================
// Spoof rejection — packet-source authentication on match commands
// ============================================
//
// Commands that mutate an active match must only be accepted from the
// match's established opponent (right MAC + right matchId). A neighbor
// device forging results for someone else's match must be ignored.

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
    MatchCommand forged(strangerMac, MatchCmd::NEVER_PRESSED, realMatchId, "xxxx", 0, false);
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

    MatchCommand stale(opponentMac, MatchCmd::DRAW_RESULT, "wrong-match-id-xxxxxxxxxxxxxxxxxxxx",
                       "opp_", 222, false);
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
    MatchCommand cmd(strangerMac, MatchCmd::SEND_MATCH_ID, "match-id-000000000000000000000000000",
                     "xxxx", 0, true);
    mm->listenForMatchEvents(cmd);

    EXPECT_FALSE(mm->getCurrentMatch().has_value());
}

// Legitimate path: command with correct MAC + matchId is accepted.
inline void matchManagerAcceptsResultFromOpponent(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);
    const char* realMatchId = mm->getCurrentMatch()->getMatchId();

    MatchCommand legit(opponentMac, MatchCmd::DRAW_RESULT, realMatchId, "opp_", 150, false);
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
    mm->setMatchLocalStartTime(1000);

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

inline void matchManagerMatchStartTimeTracking(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    EXPECT_EQ(mm->getMatchLocalStartTime(), 0);
    mm->setMatchLocalStartTime(5000);
    EXPECT_EQ(mm->getMatchLocalStartTime(), 5000);
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
    MatchCommand ack(dummyMac, MatchCmd::MATCH_ID_ACK, matchId, "boun", 0, false);
    mm->listenForMatchEvents(ack);

    EXPECT_TRUE(mm->isMatchReady());
}

// Bounty's matchIsReady becomes true immediately upon receiving SEND_MATCH_ID.
inline void matchManagerBountyMatchIsReadyAfterReceivingMatch(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-abc", "hunt", 0, true);
    mm->listenForMatchEvents(cmd);

    EXPECT_TRUE(mm->isMatchReady());
}

// clearCurrentMatch resets the matchIsReady flag along with all other match state.
inline void matchManagerClearMatchResetsMatchIsReadyFlag(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-abc", "hunt", 0, true);
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
    MatchCommand mismatch(dummyMac, MatchCmd::MATCH_ROLE_MISMATCH, matchId, "hunt2", 0, true);
    mm->listenForMatchEvents(mismatch);

    EXPECT_FALSE(mm->getCurrentMatch().has_value());
    EXPECT_FALSE(mm->isMatchReady());
}

TEST_F(MatchManagerTestSuite, abandoningAHandshakeCancelsItsPendingRetries) {
    using ::testing::_;
    std::vector<int> sentCommands;
    ON_CALL(*device.mockPeerComms, sendData(_, PktType::kMatchCommand, _, _))
    .WillByDefault(::testing::Invoke(
    [&sentCommands](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        if (len == sizeof(MatchPacket)) {
            MatchPacket packet{};
            memcpy(&packet, data, sizeof(packet));
            sentCommands.push_back(packet.command);
        }
        return 1;
    }));

    uint8_t opponent[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    matchManager->initializeMatch(opponent);
    ASSERT_FALSE(sentCommands.empty()) << "initializeMatch sent no SEND_MATCH_ID";

    fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    matchManager->sync();
    const size_t beforeClear = sentCommands.size();
    ASSERT_GT(beforeClear, 1u) << "the handshake never retransmitted";

    matchManager->clearCurrentMatch();
    fakeClock->advance(Resender::INITIAL_TIMEOUT_MS * 2 + 1);
    matchManager->sync();

    EXPECT_EQ(sentCommands.size(), beforeClear)
        << "an abandoned handshake kept retransmitting its SEND_MATCH_ID";
}

TEST_F(MatchManagerTestSuite, clearingAFinishedMatchKeepsItsResultRetrying) {
    using ::testing::_;
    std::vector<int> sentCommands;
    ON_CALL(*device.mockPeerComms, sendData(_, PktType::kMatchCommand, _, _))
    .WillByDefault(::testing::Invoke(
    [&sentCommands](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        if (len == sizeof(MatchPacket)) {
            MatchPacket packet{};
            memcpy(&packet, data, sizeof(packet));
            sentCommands.push_back(packet.command);
        }
        return 1;
    }));

    uint8_t opponent[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    matchManager->initializeMatch(opponent);
    ASSERT_FALSE(sentCommands.empty());

    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(150);
    matchManager->getMatchButtonPush()(matchManager);
    const size_t afterResult = sentCommands.size();
    ASSERT_GT(afterResult, 0u) << "the button push sent nothing";

    matchManager->clearCurrentMatch();
    fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    matchManager->sync();

    bool resultRetried = false;
    for (size_t i = afterResult; i < sentCommands.size(); ++i) {
        if (sentCommands[i] == MatchCmd::DRAW_RESULT) resultRetried = true;
    }
    EXPECT_TRUE(resultRetried)
        << "clearing a finished match stripped the retransmits of its DRAW_RESULT";
}

TEST_F(MatchManagerTestSuite, ringBufferStores32MatchesAndWrapsAtSlot0) {
    NativePrefsDriver prefs("ring-prefs", {MATCHES_PREFS_NAMESPACE});
    matchManager->initialize(player, &prefs);

    for (uint32_t i = 1; i <= 33; ++i) {
        char mid[37];
        snprintf(mid, sizeof(mid), "%08u-1234-4000-8000-000000000000", i);
        setupMatchAsBounty(mid, "hunt");
        matchManager->setHunterDrawTime(100 + static_cast<long>(i));
        matchManager->setBountyDrawTime(200 + static_cast<long>(i));
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        ASSERT_TRUE(matchManager->finalizeMatch());
    }

    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 33u);
    EXPECT_EQ(matchManager->getStoredMatchCount(), 32u);

    Match evicted{};
    EXPECT_FALSE(matchManager->readMatchBySequence(1, evicted));

    Match latest{};
    ASSERT_TRUE(matchManager->readMatchBySequence(33, latest));
    EXPECT_STREQ(latest.getMatchId(), "00000033-1234-4000-8000-000000000000");

    Match slot0{};
    ASSERT_TRUE(matchManager->readMatchFromSlot(0, slot0));
    EXPECT_STREQ(slot0.getMatchId(), "00000033-1234-4000-8000-000000000000");
    matchManager->initialize(player, &mockStorage);
}

TEST_F(MatchManagerTestSuite, collectedHighWaterMarkClampsWhenCorruptedAboveSeq) {
    NativePrefsDriver prefs("clamp-prefs", {MATCHES_PREFS_NAMESPACE});
    matchManager->initialize(player, &prefs);

    prefs.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, 5u);
    prefs.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 99u);

    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 5u);
    EXPECT_EQ(prefs.readUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0u), 5u);
    matchManager->initialize(player, &mockStorage);
}

TEST_F(MatchManagerTestSuite, sendMatchIdRejectsMissingOrSelfPlayerUuid) {
    UUIDBytes selfId{};
    UUIDBytes::parse("7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11", selfId);
    Team team{};
    strncpy(team.name, "Bounty", sizeof(team.name) - 1);
    player->applyRegistrationProfile(selfId, "LocalBounty", team, {});

    static const uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    // Missing/zero playerUuid must be rejected
    MatchCommand missingIdCmd(peerMac, MatchCmd::SEND_MATCH_ID,
                              "12345678-1234-4000-8000-000000000001", "hunt", 0, true, UUIDBytes{},
                              UUIDBytes{});
    matchManager->listenForMatchEvents(missingIdCmd);
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value());

    // Self playerUuid must be rejected
    MatchCommand selfIdCmd(peerMac, MatchCmd::SEND_MATCH_ID, "12345678-1234-4000-8000-000000000002",
                           "hunt", 0, true, selfId, UUIDBytes{});
    matchManager->listenForMatchEvents(selfIdCmd);
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value());

    // Distinct non-zero playerUuid is accepted
    UUIDBytes distinctId{};
    UUIDBytes::parse("aaaaaaaa-bbbb-4000-8000-cccccccccccc", distinctId);
    MatchCommand validCmd(peerMac, MatchCmd::SEND_MATCH_ID, "12345678-1234-4000-8000-000000000003",
                          "hunt", 0, true, distinctId, UUIDBytes{});
    matchManager->listenForMatchEvents(validCmd);
    ASSERT_TRUE(matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterPlayerId(), distinctId);
    EXPECT_EQ(matchManager->getCurrentMatch()->getBountyPlayerId(), selfId);
}

TEST_F(MatchManagerTestSuite, cabledFdnMatchTransferAndSpoolBatchUpload) {
    using ::testing::_;
    NativePrefsDriver pdnPrefs("pdn-prefs", {MATCHES_PREFS_NAMESPACE});
    NativePrefsDriver fdnPrefs("fdn-prefs", {FDN_MATCH_SPOOL_NAMESPACE});
    matchManager->initialize(player, &pdnPrefs);

    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    ON_CALL(*device.mockPeerComms, getPeerCommsState())
    .WillByDefault(::testing::Return(PeerCommsState::ENABLED));
    ON_CALL(*fdnDevice.mockPeerComms, getPeerCommsState())
    .WillByDefault(::testing::Return(PeerCommsState::ENABLED));
    FdnMatchSpool fdnSpool(fdnDevice.wirelessManager, &fdnPrefs, &fdnRdc);

    const uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    const uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x02};

    // Route kMatchCollection frames between PDN and FDN
    ON_CALL(*device.mockPeerComms, sendData(_, PktType::kMatchCollection, _, _))
    .WillByDefault(::testing::Invoke(
    [&fdnSpool, &pdnMac](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        fdnSpool.onMatchCollectionPacket(pdnMac, data, len);
        return 1;
    }));
    ON_CALL(*fdnDevice.mockPeerComms, sendData(_, PktType::kMatchCollection, _, _))
    .WillByDefault(::testing::Invoke(
    [this, &fdnMac](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        device.mockPeerComms->triggerPacketCallback(fdnMac, PktType::kMatchCollection, data, len);
        return 1;
    }));

    // Finalize one match on the PDN
    setupMatchAsBounty("11111111-2222-4000-8000-333333333333", "hunt");
    matchManager->setHunterDrawTime(180);
    matchManager->setBountyDrawTime(120);
    matchManager->setReceivedButtonPush();
    matchManager->setReceivedDrawResult();
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    ASSERT_EQ(matchManager->getCollectedHighWaterMark(), 0u);

    // Uncabled ACK from stranger MAC is ignored
    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    MatchTransferAckPacket forgedAck{static_cast<uint8_t>(MatchCollectionCmd::ACK), 1u};
    device.mockPeerComms->triggerPacketCallback(strangerMac, PktType::kMatchCollection,
                                                reinterpret_cast<const uint8_t*>(&forgedAck),
                                                sizeof(forgedAck));
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 0u);

    // Connect PDN and FDN on their serial jacks
    fakeRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fakeRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::FDN);
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, fdnMac);

    fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
    fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, pdnMac);

    // Sync transfers the match to FDN spool and advances PDN collected_high_water_mark on ACK
    matchManager->sync();
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 1u);
    EXPECT_EQ(fdnSpool.getStagedCount(), 1u);

    // Batch upload from FDN spool: server error retains spool; 204 retires spool
    std::vector<HttpRequest> httpRequests;
    ON_CALL(*fdnDevice.mockHttpClient, queueRequest(_))
    .WillByDefault(::testing::Invoke([&httpRequests](HttpRequest&& req) {
        httpRequests.push_back(std::move(req));
        return true;
    }));

    ASSERT_TRUE(fdnSpool.uploadPendingBatch());
    ASSERT_EQ(httpRequests.size(), 1u);
    EXPECT_EQ(httpRequests[0].path, "/quickdraw-matches");
    httpRequests[0].onError(WirelessErrorInfo{WirelessError::SERVER_ERROR, "HTTP 500"});
    EXPECT_EQ(fdnSpool.getStagedCount(), 1u) << "5xx error must retain staged batch";

    httpRequests.clear();
    ASSERT_TRUE(fdnSpool.uploadPendingBatch());
    ASSERT_EQ(httpRequests.size(), 1u);
    httpRequests[0].onSuccess("");  // HTTP 204 empty body
    EXPECT_EQ(fdnSpool.getStagedCount(), 0u) << "HTTP 204 must retire uploaded batch";
    matchManager->initialize(player, &mockStorage);
}

TEST_F(MatchManagerTestSuite, fdnSpoolWithholdsAckWhenFullAt64Records) {
    using ::testing::_;
    NativePrefsDriver fdnPrefs("fdn-full-prefs", {FDN_MATCH_SPOOL_NAMESPACE});
    MockDevice fdnDevice;
    ON_CALL(*fdnDevice.mockPeerComms, getPeerCommsState())
    .WillByDefault(::testing::Return(PeerCommsState::ENABLED));
    FakeRemoteDeviceCoordinator fdnRdc;
    FdnMatchSpool fdnSpool(fdnDevice.wirelessManager, &fdnPrefs, &fdnRdc);

    const uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
    fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, pdnMac);

    size_t acksSent = 0;
    ON_CALL(*fdnDevice.mockPeerComms, sendData(_, PktType::kMatchCollection, _, _))
    .WillByDefault(
    ::testing::Invoke([&acksSent](const uint8_t*, PktType, const uint8_t*, const size_t) {
        acksSent++;
        return 1;
    }));

    for (uint32_t seq = 1; seq <= 65; ++seq) {
        char mid[37];
        snprintf(mid, sizeof(mid), "%08u-aaaa-4000-8000-bbbbbbbbbbbb", seq);
        Match m(mid, "hunt", true);
        MatchTransferPacket pkt{};
        pkt.cmd = static_cast<uint8_t>(MatchCollectionCmd::RECORD);
        pkt.matchSeq = seq;
        m.serialize(pkt.matchData);
        fdnSpool.onMatchCollectionPacket(pdnMac, reinterpret_cast<const uint8_t*>(&pkt),
                                         sizeof(pkt));
    }

    EXPECT_EQ(fdnSpool.getStagedCount(), 64u);
    EXPECT_EQ(acksSent, 64u) << "65th match must not be ACKed while spool is full";
}
