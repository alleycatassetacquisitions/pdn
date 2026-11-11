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

        using ::testing::_;
        radioTap.attach(device.mockPeerComms);

        matchManager = new MatchManager(device.wirelessManager);
        player = new Player();
        char playerId[] = "test";
        player->setUserID(playerId);
        matchManager->initialize(player, &mockStorage);
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
        // Tests point the manager at a NativePrefsDriver local to their body;
        // its destructor must not reach for one that is already gone.
        matchManager->initialize(player, &mockStorage);
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

    /**
     * Bounty-side match with its MATCH_ID_ACK reported delivered. Both players have
     * non-nil UUIDs, so finalizeMatch can store it.
     */
    void setupMatchAsBounty(const char* matchId = "test-match-id") {
        UUIDBytes selfId{};
        UUIDBytes::parse("0b0b0b0b-0000-4000-8000-000000000001", selfId);
        Team bounty{};
        strncpy(bounty.name, "Bounty", sizeof(bounty.name) - 1);
        player->applyRegistrationProfile(selfId, "LocalBounty", bounty, {});
        UUIDBytes hunterUuid{};
        UUIDBytes::parse("0a0a0a0a-0000-4000-8000-000000000002", hunterUuid);
        static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, matchId, 0, true, hunterUuid);
        matchManager->listenForMatchEvents(cmd);
        radioTap.confirmLastSend();
    }

    MatchManager* matchManager;
    Player* player;
    MockDevice device;
    MockStorage mockStorage;
    FakeRemoteDeviceCoordinator fakeRdc;
    FakePlatformClock* fakeClock;
    QuickdrawRadioTap radioTap;
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

inline void matchManagerResultSnapshotCarriesOnlyThisMatchsPenalty(MatchManagerTestSuite* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    parameterizedCallbackFunction masher = suite->matchManager->getButtonMasher();
    masher(suite->matchManager);
    masher(suite->matchManager);

    suite->matchManager->setMatchLocalStartTime(suite->fakeClock->milliseconds());
    suite->fakeClock->advance(200);
    suite->matchManager->getMatchButtonPush()(suite->matchManager);
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 350u);
    suite->matchManager->finalizeMatch();
    EXPECT_EQ(suite->matchManager->getLastMatchDisplay().penaltyMs, 150u);

    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->sendNeverPressed(4000);
    suite->matchManager->finalizeMatch();
    EXPECT_EQ(suite->matchManager->getLastMatchDisplay().penaltyMs, 0u)
    << "a match that was never pressed showed the previous match's penalty";
}

// ============================================
// Match Initialization / Handshake Tests
// ============================================

inline void matchManagerInitializeCreatesMatch(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    ASSERT_TRUE(mm->getCurrentMatch().has_value());
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
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "received-match", 0, true);
    mm->listenForMatchEvents(cmd);

    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_STREQ(mm->getCurrentMatch()->getMatchId(), "received-match");
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
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-3", 0, true);
    mm->listenForMatchEvents(cmd);
    mm->setHunterDrawTime(400);
    mm->setBountyDrawTime(250);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-4", 0, true);
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
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-6", 0, true);
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
    MatchCommand forged(strangerMac, MatchCmd::NEVER_PRESSED, realMatchId, 0, false);
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
                       222, false);
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
                     0, true);
    mm->listenForMatchEvents(cmd);

    EXPECT_FALSE(mm->getCurrentMatch().has_value());
}

// Legitimate path: command with correct MAC + matchId is accepted.
inline void matchManagerAcceptsResultFromOpponent(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);
    const char* realMatchId = mm->getCurrentMatch()->getMatchId();

    MatchCommand legit(opponentMac, MatchCmd::DRAW_RESULT, realMatchId, 150, false);
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
    MatchCommand ack(dummyMac, MatchCmd::MATCH_ID_ACK, matchId, 0, false);
    mm->listenForMatchEvents(ack);

    EXPECT_TRUE(mm->isMatchReady());
}

inline void matchManagerBountyReadiesWhenItsAckIsDelivered(MatchManagerTestSuite* suite) {
    suite->player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-abc", 0, true);
    suite->matchManager->listenForMatchEvents(cmd);
    EXPECT_FALSE(suite->matchManager->isMatchReady())
    << "the bounty started its countdown before the hunter could have heard the ack";

    suite->radioTap.confirmLastSend();
    EXPECT_TRUE(suite->matchManager->isMatchReady());
}

// Every match frame to this peer shares the channel, and a late report for an earlier one
// still fires the delivery callback; only the ack's own report may ready the bounty.
inline void matchManagerBountyIgnoresDeliveryOfAnotherFrame(MatchManagerTestSuite* suite) {
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->player->setIsHunter(true);
    MatchCommand sameRole(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-aaa", 0, true);
    suite->matchManager->listenForMatchEvents(sameRole);
    suite->player->setIsHunter(false);
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-abc", 0, true);
    suite->matchManager->listenForMatchEvents(cmd);
    ASSERT_EQ(suite->radioTap.sentFrames.size(), 2u);
    ASSERT_EQ(suite->radioTap.sentFrames[0].command, MatchCmd::MATCH_ROLE_MISMATCH);

    suite->radioTap.confirmSend(suite->radioTap.sentFrames[0]);
    EXPECT_FALSE(suite->matchManager->isMatchReady())
    << "the role-mismatch reply's delivery was taken for the ack's";

    suite->radioTap.confirmSend(suite->radioTap.sentFrames[1]);
    EXPECT_TRUE(suite->matchManager->isMatchReady());
}

// No delivery report ever came, so the hunter may never have heard the ack.
inline void matchManagerBountyDropsTheMatchWhenItsAckIsAbandoned(MatchManagerTestSuite* suite) {
    suite->player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-abc", 0, true);
    suite->matchManager->listenForMatchEvents(cmd);
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    suite->fakeClock->advance(Resender::RETRANSMIT_SPAN_MS + 1);
    suite->matchManager->sync();

    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_FALSE(suite->matchManager->isMatchReady());
}

// clearCurrentMatch resets the matchIsReady flag along with all other match state.
inline void matchManagerClearMatchResetsMatchIsReadyFlag(MatchManagerTestSuite* suite) {
    suite->setupMatchAsBounty("match-id-abc");
    ASSERT_TRUE(suite->matchManager->isMatchReady());

    suite->matchManager->clearCurrentMatch();

    EXPECT_FALSE(suite->matchManager->isMatchReady());
    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
}

// Receiving MATCH_ROLE_MISMATCH clears the initiator's active match and leaves matchIsReady false.
inline void matchManagerRoleMismatchClearsInitiatorMatch(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    ASSERT_FALSE(mm->isMatchReady());

    const char* matchId = mm->getCurrentMatch()->getMatchId();
    MatchCommand mismatch(dummyMac, MatchCmd::MATCH_ROLE_MISMATCH, matchId, 0, true);
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
        setupMatchAsBounty(mid);
        matchManager->setHunterDrawTime(100 + static_cast<long>(i));
        matchManager->setBountyDrawTime(200 + static_cast<long>(i));
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        ASSERT_TRUE(matchManager->finalizeMatch());
    }

    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 33u);

    Match evicted{};
    EXPECT_FALSE(matchManager->readMatchBySequence(1, evicted));

    Match latest{};
    ASSERT_TRUE(matchManager->readMatchBySequence(33, latest));
    EXPECT_STREQ(latest.getMatchId(), "00000033-1234-4000-8000-000000000000");

    uint8_t slot0[MATCH_BINARY_SIZE] = {};
    ASSERT_EQ(prefs.readBytes(MATCHES_PREFS_NAMESPACE, "match_0", slot0, MATCH_BINARY_SIZE),
              MATCH_BINARY_SIZE);
    Match slot0Match{};
    slot0Match.deserialize(slot0);
    EXPECT_STREQ(slot0Match.getMatchId(), "00000033-1234-4000-8000-000000000000");
}

TEST_F(MatchManagerTestSuite, initializeDropsRecordsFromBeforeTheRing) {
    NativePrefsDriver prefs("legacy-prefs", {MATCHES_PREFS_NAMESPACE});
    prefs.write(MATCHES_PREFS_NAMESPACE, "match_40", R"({"match_id":"old"})");
    prefs.writeUChar(MATCHES_PREFS_NAMESPACE, "count", 41);

    matchManager->initialize(player, &prefs);

    EXPECT_EQ(prefs.read(MATCHES_PREFS_NAMESPACE, "match_40", ""), "");
    EXPECT_EQ(prefs.readUChar(MATCHES_PREFS_NAMESPACE, "count", 0), 0);
}

TEST_F(MatchManagerTestSuite, sendMatchIdRejectsSelfPlayerUuidButAcceptsDemoOpponent) {
    NativePrefsDriver prefs("demo-prefs", {MATCHES_PREFS_NAMESPACE});
    matchManager->initialize(player, &prefs);
    UUIDBytes selfId{};
    UUIDBytes::parse("7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11", selfId);
    Team team{};
    strncpy(team.name, "Bounty", sizeof(team.name) - 1);
    player->applyRegistrationProfile(selfId, "LocalBounty", team, {});

    static const uint8_t peerMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    MatchCommand demoCmd(peerMac, MatchCmd::SEND_MATCH_ID, "12345678-1234-4000-8000-000000000001",
                         0, true, UUIDBytes{}, UUIDBytes{});
    matchManager->listenForMatchEvents(demoCmd);
    ASSERT_TRUE(matchManager->getCurrentMatch().has_value());
    matchManager->setHunterDrawTime(300);
    matchManager->setBountyDrawTime(200);
    matchManager->setReceivedButtonPush();
    matchManager->setReceivedDrawResult();
    EXPECT_TRUE(matchManager->finalizeMatch());
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 0u) << "a demo match is never stored";

    MatchCommand selfIdCmd(peerMac, MatchCmd::SEND_MATCH_ID, "12345678-1234-4000-8000-000000000002",
                           0, true, selfId, UUIDBytes{});
    matchManager->listenForMatchEvents(selfIdCmd);
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value());

    UUIDBytes distinctId{};
    UUIDBytes::parse("aaaaaaaa-bbbb-4000-8000-cccccccccccc", distinctId);
    MatchCommand validCmd(peerMac, MatchCmd::SEND_MATCH_ID, "12345678-1234-4000-8000-000000000003",
                          0, true, distinctId, UUIDBytes{});
    matchManager->listenForMatchEvents(validCmd);
    ASSERT_TRUE(matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterPlayerId(), distinctId);
    EXPECT_EQ(matchManager->getCurrentMatch()->getBountyPlayerId(), selfId);
}

// didWin() cannot supply the record's winner: on a tie it is false on both devices.
TEST_F(MatchManagerTestSuite, bothDevicesRecordTheSameWinnerAndNoTie) {
    NativePrefsDriver prefs("winner-prefs", {MATCHES_PREFS_NAMESPACE});
    matchManager->initialize(player, &prefs);
    UUIDBytes hunterUuid{};
    UUIDBytes::parse("0a0a0a0a-0000-4000-8000-000000000002", hunterUuid);
    UUIDBytes bountyUuid{};
    UUIDBytes::parse("0b0b0b0b-0000-4000-8000-000000000001", bountyUuid);
    static const uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    const auto finishOnBountyDevice = [&](unsigned long hunterMs, unsigned long bountyMs) {
        setupMatchAsBounty("51111111-2222-4000-8000-333333333333");
        matchManager->setHunterDrawTime(hunterMs);
        matchManager->setBountyDrawTime(bountyMs);
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        ASSERT_TRUE(matchManager->finalizeMatch());
    };
    const auto finishOnHunterDevice = [&](unsigned long hunterMs, unsigned long bountyMs) {
        Team hunters{};
        strncpy(hunters.name, "Hunter", sizeof(hunters.name) - 1);
        player->applyRegistrationProfile(hunterUuid, "LocalHunter", hunters, {});
        uint8_t mac[6];
        memcpy(mac, opponentMac, 6);
        matchManager->initializeMatch(mac);
        MatchCommand ack(opponentMac, MatchCmd::MATCH_ID_ACK,
                         matchManager->getCurrentMatch()->getMatchId(), 0, false, bountyUuid);
        matchManager->listenForMatchEvents(ack);
        matchManager->setHunterDrawTime(hunterMs);
        matchManager->setBountyDrawTime(bountyMs);
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        ASSERT_TRUE(matchManager->finalizeMatch());
    };

    finishOnBountyDevice(180, 120);
    finishOnHunterDevice(180, 120);
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 2u);
    Match onBountyDevice{};
    Match onHunterDevice{};
    ASSERT_TRUE(matchManager->readMatchBySequence(1, onBountyDevice));
    ASSERT_TRUE(matchManager->readMatchBySequence(2, onHunterDevice));
    EXPECT_EQ(onBountyDevice.getWinnerPlayerId(), bountyUuid);
    EXPECT_EQ(onHunterDevice.getWinnerPlayerId(), bountyUuid);

    // Both players lose a tie, and the server takes no record without a winner.
    finishOnBountyDevice(150, 150);
    finishOnHunterDevice(150, 150);
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 2u) << "a tie was stored";
}

// A presser slowed past the pity time still beat an opponent who never drew, and
// both result screens said so; the record must name the same winner.
TEST_F(MatchManagerTestSuite, recordedWinnerRanksNeverPressedAboveTimes) {
    NativePrefsDriver prefs("never-prefs", {MATCHES_PREFS_NAMESPACE});
    matchManager->initialize(player, &prefs);
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    Match stored{};

    setupMatchAsBounty("61111111-2222-4000-8000-333333333333");
    matchManager->setHunterDrawTime(5000);
    matchManager->setBountyDrawTime(1000);
    matchManager->setReceivedDrawResult();
    matchManager->setNeverPressed();
    ASSERT_FALSE(matchManager->didWin());
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_TRUE(matchManager->readMatchBySequence(1, stored));
    EXPECT_EQ(stored.getWinnerPlayerId(), stored.getHunterPlayerId());

    setupMatchAsBounty("62222222-2222-4000-8000-333333333333");
    matchManager->setBountyDrawTime(5000);
    matchManager->setReceivedButtonPush();
    MatchCommand neverPressed(hunterMac, MatchCmd::NEVER_PRESSED,
                              "62222222-2222-4000-8000-333333333333", 1000, true);
    matchManager->listenForMatchEvents(neverPressed);
    ASSERT_TRUE(matchManager->didWin());
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_TRUE(matchManager->readMatchBySequence(2, stored));
    EXPECT_EQ(stored.getWinnerPlayerId(), stored.getBountyPlayerId());

    // Neither side drew: each screen shows its own outcome, so there is no shared winner.
    setupMatchAsBounty("63333333-2222-4000-8000-333333333333");
    MatchCommand bothNever(hunterMac, MatchCmd::NEVER_PRESSED,
                           "63333333-2222-4000-8000-333333333333", 1000, true);
    matchManager->listenForMatchEvents(bothNever);
    matchManager->setNeverPressed();
    ASSERT_TRUE(matchManager->finalizeMatch());
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 2u);
}

TEST_F(MatchManagerTestSuite, collectedHighWaterMarkClampsWhenCorruptedAboveSeq) {
    NativePrefsDriver prefs("clamp-prefs", {MATCHES_PREFS_NAMESPACE});
    prefs.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, 5u);
    prefs.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 99u);
    matchManager->initialize(player, &prefs);

    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 5u);
    EXPECT_EQ(prefs.readUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0u), 5u)
    << "the clamp must persist, or matches 6..99 would read as collected once stored";
}

TEST_F(MatchManagerTestSuite, unreadableStoredMatchIsSkippedNotRetriedForever) {
    NativePrefsDriver pdnPrefs("pdn-torn-prefs", {MATCHES_PREFS_NAMESPACE});
    matchManager->initialize(player, &pdnPrefs);
    for (int i = 0; i < 2; ++i) {
        setupMatchAsBounty(i == 0 ? "33333333-2222-4000-8000-333333333333"
                                  : "44444444-2222-4000-8000-333333333333");
        matchManager->setHunterDrawTime(180);
        matchManager->setBountyDrawTime(120);
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        EXPECT_TRUE(matchManager->finalizeMatch());
    }
    const uint8_t torn[3] = {1, 2, 3};
    pdnPrefs.writeBytes(MATCHES_PREFS_NAMESPACE, "match_0", torn, sizeof(torn));

    const uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x06};
    fakeRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fakeRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::FDN);
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, fdnMac);
    ON_CALL(*device.mockPeerComms, getPeerCommsState())
    .WillByDefault(::testing::Return(PeerCommsState::ENABLED));

    std::vector<uint32_t> sentSeqs;
    ON_CALL(*device.mockPeerComms,
            sendData(::testing::_, PktType::kMatchCollection, ::testing::_, ::testing::_))
    .WillByDefault(
    ::testing::Invoke([&sentSeqs](const uint8_t*, PktType, const uint8_t* data, const size_t) {
        MatchRecordPacket pkt{};
        memcpy(&pkt, data, sizeof(pkt));
        sentSeqs.push_back(pkt.matchSeq);
        return 1;
    }));

    matchManager->sync();
    matchManager->sync();
    EXPECT_EQ(sentSeqs, (std::vector<uint32_t>{2u})) << "record 1 is unreadable and skipped";
}

TEST_F(MatchManagerTestSuite, matchTransferSendsOncePerWindowUntilAcked) {
    NativePrefsDriver pdnPrefs("pdn-window-prefs", {MATCHES_PREFS_NAMESPACE});
    matchManager->initialize(player, &pdnPrefs);
    setupMatchAsBounty("55555555-2222-4000-8000-333333333333");
    matchManager->setHunterDrawTime(180);
    matchManager->setBountyDrawTime(120);
    matchManager->setReceivedButtonPush();
    matchManager->setReceivedDrawResult();
    ASSERT_TRUE(matchManager->finalizeMatch());

    const uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x07};
    fakeRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fakeRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::FDN);
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, fdnMac);
    ON_CALL(*device.mockPeerComms, getPeerCommsState())
    .WillByDefault(::testing::Return(PeerCommsState::ENABLED));
    int sends = 0;
    ON_CALL(*device.mockPeerComms,
            sendData(::testing::_, PktType::kMatchCollection, ::testing::_, ::testing::_))
    .WillByDefault(
    ::testing::Invoke([&sends](const uint8_t*, PktType, const uint8_t*, const size_t) {
        sends++;
        return 1;
    }));

    MatchRecordAckPacket ack{};
    ack.matchSeq = 1;
    device.mockPeerComms->triggerPacketCallback(
    fdnMac, PktType::kMatchCollectionAck, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    matchManager->sync();
    ASSERT_EQ(sends, 1) << "an ack for a record not yet sent marked it collected";
    matchManager->sync();
    EXPECT_EQ(sends, 1) << "one send per window, not per tick";
    fakeClock->advance(MATCH_TRANSFER_WINDOW_MS + 1);
    matchManager->sync();
    EXPECT_EQ(sends, 2) << "an unacked record is resent once the window ends";

    device.mockPeerComms->triggerPacketCallback(
    fdnMac, PktType::kMatchCollectionAck, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    fakeClock->advance(MATCH_TRANSFER_WINDOW_MS + 1);
    matchManager->sync();
    EXPECT_EQ(sends, 2) << "an acked record is not resent";
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

    // Route records PDN-to-FDN and acks FDN-to-PDN
    ON_CALL(*device.mockPeerComms, sendData(_, PktType::kMatchCollection, _, _))
    .WillByDefault(::testing::Invoke(
    [&fdnDevice, &pdnMac](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        fdnDevice.mockPeerComms->triggerPacketCallback(pdnMac, PktType::kMatchCollection, data,
                                                       len);
        return 1;
    }));
    ON_CALL(*fdnDevice.mockPeerComms, sendData(_, PktType::kMatchCollectionAck, _, _))
    .WillByDefault(::testing::Invoke(
    [this, &fdnMac](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
        device.mockPeerComms->triggerPacketCallback(fdnMac, PktType::kMatchCollectionAck, data,
                                                    len);
        return 1;
    }));

    // Finalize one match on the PDN
    setupMatchAsBounty("11111111-2222-4000-8000-333333333333");
    matchManager->setHunterDrawTime(180);
    matchManager->setBountyDrawTime(120);
    matchManager->setReceivedButtonPush();
    matchManager->setReceivedDrawResult();
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    ASSERT_EQ(matchManager->getCollectedHighWaterMark(), 0u);

    // Uncabled ACK from stranger MAC is ignored
    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    MatchRecordAckPacket forgedAck{1u};
    device.mockPeerComms->triggerPacketCallback(strangerMac, PktType::kMatchCollectionAck,
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

    // Sync transfers the match to the FDN spool and its ack advances getCollectedHighWaterMark()
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

    std::vector<bool> outcomes;
    auto onComplete = [&outcomes](bool uploaded) { outcomes.push_back(uploaded); };
    ASSERT_TRUE(fdnSpool.uploadPendingBatch(onComplete));
    ASSERT_EQ(httpRequests.size(), 1u);
    EXPECT_EQ(httpRequests[0].path, "/quickdraw-matches");
    httpRequests[0].onError(WirelessErrorInfo{WirelessError::SERVER_ERROR, "HTTP 500"});
    EXPECT_EQ(fdnSpool.getStagedCount(), 1u) << "5xx error must retain staged batch";

    httpRequests.clear();
    ASSERT_TRUE(fdnSpool.uploadPendingBatch(onComplete));
    ASSERT_EQ(httpRequests.size(), 1u);
    httpRequests[0].onSuccess("");  // HTTP 204 empty body
    EXPECT_EQ(fdnSpool.getStagedCount(), 0u) << "HTTP 204 must retire uploaded batch";
    EXPECT_EQ(outcomes, (std::vector<bool>{false, true}));
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
    ON_CALL(*fdnDevice.mockPeerComms, sendData(_, PktType::kMatchCollectionAck, _, _))
    .WillByDefault(
    ::testing::Invoke([&acksSent](const uint8_t*, PktType, const uint8_t*, const size_t) {
        acksSent++;
        return 1;
    }));

    auto send = [&pdnMac](FdnMatchSpool& spool, uint32_t seq) {
        char mid[37];
        snprintf(mid, sizeof(mid), "%08u-aaaa-4000-8000-bbbbbbbbbbbb", seq);
        Match m(mid);
        MatchRecordPacket pkt{};
        pkt.matchSeq = seq;
        m.serialize(pkt.matchData);
        spool.onMatchRecord(pdnMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    };
    for (uint32_t seq = 1; seq <= 65; ++seq)
        send(fdnSpool, seq);

    EXPECT_EQ(fdnSpool.getStagedCount(), 64u);
    EXPECT_EQ(acksSent, 64u) << "65th match must not be ACKed while spool is full";

    send(fdnSpool, 64);
    EXPECT_EQ(acksSent, 65u) << "a resend after a lost ack is re-acked even when full";
    EXPECT_EQ(fdnSpool.getStagedCount(), 64u);

    FdnMatchSpool rebooted(fdnDevice.wirelessManager, &fdnPrefs, &fdnRdc);
    send(rebooted, 1);
    EXPECT_EQ(acksSent, 66u) << "staged records are known again after a reboot";
    EXPECT_EQ(rebooted.getStagedCount(), 64u);
}

TEST_F(MatchManagerTestSuite, matchCollectionDropsWrongLengthFrames) {
    NativePrefsDriver pdnPrefs("pdn-len-prefs", {MATCHES_PREFS_NAMESPACE});
    NativePrefsDriver fdnPrefs("fdn-len-prefs", {FDN_MATCH_SPOOL_NAMESPACE});
    matchManager->initialize(player, &pdnPrefs);
    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    FdnMatchSpool fdnSpool(fdnDevice.wirelessManager, &fdnPrefs, &fdnRdc);

    const uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x04};
    const uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x04};
    fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
    fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, pdnMac);

    Match m("12345678-eeee-4000-8000-ffffffffffff");
    MatchRecordPacket pkt{};
    pkt.matchSeq = 1;
    m.serialize(pkt.matchData);
    fdnSpool.onMatchRecord(pdnMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt) - 1);
    EXPECT_EQ(fdnSpool.getStagedCount(), 0u);

    setupMatchAsBounty("22222222-2222-4000-8000-333333333333");
    matchManager->setHunterDrawTime(180);
    matchManager->setBountyDrawTime(120);
    matchManager->setReceivedButtonPush();
    matchManager->setReceivedDrawResult();
    EXPECT_TRUE(matchManager->finalizeMatch());
    fakeRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fakeRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::FDN);
    fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, fdnMac);
    const uint8_t longAck[sizeof(MatchRecordAckPacket) + 1] = {1, 0, 0, 0, 0};
    device.mockPeerComms->triggerPacketCallback(fdnMac, PktType::kMatchCollectionAck, longAck,
                                                sizeof(longAck));
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 0u);
    matchManager->initialize(player, &mockStorage);
}

TEST_F(MatchManagerTestSuite, fdnSpoolDropsUnreadableHeadInsteadOfFailingEveryBatch) {
    using ::testing::_;
    NativePrefsDriver fdnPrefs("fdn-corrupt-prefs", {FDN_MATCH_SPOOL_NAMESPACE});
    MockDevice fdnDevice;
    ON_CALL(*fdnDevice.mockPeerComms, getPeerCommsState())
    .WillByDefault(::testing::Return(PeerCommsState::ENABLED));
    FakeRemoteDeviceCoordinator fdnRdc;
    FdnMatchSpool fdnSpool(fdnDevice.wirelessManager, &fdnPrefs, &fdnRdc);

    const uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
    fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
    fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, pdnMac);

    for (uint32_t seq = 1; seq <= 2; ++seq) {
        char mid[37];
        snprintf(mid, sizeof(mid), "%08u-cccc-4000-8000-dddddddddddd", seq);
        Match m(mid);
        MatchRecordPacket pkt{};
        pkt.matchSeq = seq;
        m.serialize(pkt.matchData);
        fdnSpool.onMatchRecord(pdnMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    }
    const uint8_t torn[3] = {1, 2, 3};
    fdnPrefs.writeBytes(FDN_MATCH_SPOOL_NAMESPACE, "spool_0", torn, sizeof(torn));

    std::vector<HttpRequest> httpRequests;
    ON_CALL(*fdnDevice.mockHttpClient, queueRequest(_))
    .WillByDefault(::testing::Invoke([&httpRequests](HttpRequest&& req) {
        httpRequests.push_back(std::move(req));
        return true;
    }));

    ASSERT_TRUE(fdnSpool.uploadPendingBatch(nullptr));
    ASSERT_EQ(httpRequests.size(), 1u) << "the readable record still goes up";
    EXPECT_EQ(fdnSpool.getStagedCount(), 1u);
    httpRequests[0].onSuccess("");
    EXPECT_EQ(fdnSpool.getStagedCount(), 0u);
}

TEST_F(MatchManagerTestSuite, fdnSpoolUploadStopsShortOfAnUnreadableRecord) {
    using ::testing::_;
    NativePrefsDriver fdnPrefs("fdn-mid-prefs", {FDN_MATCH_SPOOL_NAMESPACE});
    MockDevice fdnDevice;
    ON_CALL(*fdnDevice.mockPeerComms, getPeerCommsState())
    .WillByDefault(::testing::Return(PeerCommsState::ENABLED));
    FakeRemoteDeviceCoordinator fdnRdc;
    FdnMatchSpool fdnSpool(fdnDevice.wirelessManager, &fdnPrefs, &fdnRdc);

    const uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x05};
    fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
    fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, pdnMac);

    for (uint32_t seq = 1; seq <= 3; ++seq) {
        char mid[37];
        snprintf(mid, sizeof(mid), "%08u-dddd-4000-8000-eeeeeeeeeeee", seq);
        Match m(mid);
        MatchRecordPacket pkt{};
        pkt.matchSeq = seq;
        m.serialize(pkt.matchData);
        fdnSpool.onMatchRecord(pdnMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    }
    const uint8_t torn[3] = {1, 2, 3};
    fdnPrefs.writeBytes(FDN_MATCH_SPOOL_NAMESPACE, "spool_1", torn, sizeof(torn));

    std::vector<HttpRequest> httpRequests;
    ON_CALL(*fdnDevice.mockHttpClient, queueRequest(_))
    .WillByDefault(::testing::Invoke([&httpRequests](HttpRequest&& req) {
        httpRequests.push_back(std::move(req));
        return true;
    }));

    ASSERT_TRUE(fdnSpool.uploadPendingBatch(nullptr));
    ASSERT_EQ(httpRequests.size(), 1u) << "the record ahead of the torn one still goes up";
    httpRequests[0].onSuccess("");
    EXPECT_EQ(fdnSpool.getStagedCount(), 2u);

    ASSERT_TRUE(fdnSpool.uploadPendingBatch(nullptr));
    ASSERT_EQ(httpRequests.size(), 2u);
    EXPECT_EQ(fdnSpool.getStagedCount(), 1u) << "the torn record is dropped once it is the oldest";
    httpRequests[1].onSuccess("");
    EXPECT_EQ(fdnSpool.getStagedCount(), 0u);
}

TEST(MatchesApiUpload, unqueuedRequestFailsClosed) {
    ::testing::NiceMock<MockPeerComms> peerComms;
    ::testing::NiceMock<MockHttpClient> httpClient;
    ON_CALL(httpClient, queueRequest(::testing::_)).WillByDefault(::testing::Return(false));
    WirelessManager wirelessManager(&peerComms, &httpClient);

    bool errored = false;
    MatchesApi::uploadQuickdrawMatches(
    &wirelessManager, 1, [](size_t, QuickdrawMatchRecord&) { return true; },
    [](const QuickdrawMatchRejection&) {}, [](const QuickdrawUploadResult&) {},
    [&errored](const WirelessErrorInfo&) { errored = true; });
    EXPECT_TRUE(errored) << "an unqueued upload must not leave its caller waiting";
}
