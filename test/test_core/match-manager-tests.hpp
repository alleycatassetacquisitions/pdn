#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device/drivers/native/native-prefs-driver.hpp"
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "player-provider-tests.hpp"
#include "utility-tests.hpp"

inline constexpr const char LOCAL_PLAYER_UUID[] = "11111111-1111-4111-8111-111111111111";
inline constexpr const char OPPONENT_PLAYER_UUID[] = "22222222-2222-4222-8222-222222222222";
inline constexpr const char OPPONENT_TEAM_UUID[] = "33333333-3333-4333-8333-333333333333";
// Older firmware's JSON matches. Nothing in this firmware may write or clear them.
inline constexpr const char LEGACY_MATCHES_NAMESPACE[] = "matches";

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
        applyTestRegistration(player, LOCAL_PLAYER_UUID, "Local");
        matchManager->initialize(player, &storage);
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
    // SEND_MATCH_ID command from a fake hunter, then reporting the MATCH_ID_ACK delivered.
    void setupMatchAsBounty(const char* matchId = "test-match-id",
                            const UUIDBytes& hunterPlayerId = makeTestUuid(OPPONENT_PLAYER_UUID)) {
        player->setIsHunter(false);
        static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, matchId, 0, true, hunterPlayerId,
                         makeTestUuid(OPPONENT_TEAM_UUID));
        matchManager->listenForMatchEvents(cmd);
        radioTap.confirmLastSend();
    }

    /** Hunter side: the bounty acks the match with its own player and team. */
    void setupAckedMatchAsHunter(
    const UUIDBytes& opponentPlayerId = makeTestUuid(OPPONENT_PLAYER_UUID)) {
        setupMatchAsHunter();
        static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        MatchCommand ack(dummyMac, MatchCmd::MATCH_ID_ACK,
                         matchManager->getCurrentMatch()->getMatchId(), 0, false, opponentPlayerId,
                         makeTestUuid(OPPONENT_TEAM_UUID));
        matchManager->listenForMatchEvents(ack);
    }

    /** The hunter draws in 200 ms against 300 ms and the match finalizes. */
    bool playHunterWin(const UUIDBytes& opponentPlayerId = makeTestUuid(OPPONENT_PLAYER_UUID)) {
        setupAckedMatchAsHunter(opponentPlayerId);
        matchManager->setHunterDrawTime(200);
        matchManager->setBountyDrawTime(300);
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        return matchManager->finalizeMatch();
    }

    /** Record sequenceNumber as stored in the ring. */
    Match storedMatch(uint32_t sequenceNumber) {
        MatchRecord record{};
        EXPECT_TRUE(matchManager->readRecord(sequenceNumber, record));
        Match match;
        match.deserialize(reinterpret_cast<const uint8_t*>(&record));
        return match;
    }

    MatchManager* matchManager;
    Player* player;
    MockDevice device;
    NativePrefsDriver storage{"match-manager-test",
                              {MATCH_RING_PREFS_NAMESPACE, LEGACY_MATCHES_NAMESPACE}};
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

    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 200u)
    << "the record keeps the raw time";
    ASSERT_NE(suite->radioTap.lastFrame(), nullptr);
    EXPECT_EQ(suite->radioTap.lastFrame()->playerDrawTime, 200);
    EXPECT_EQ(suite->radioTap.lastFrame()->boostMs, 50u);
    suite->matchManager->finalizeMatch();
    EXPECT_EQ(suite->matchManager->getLastMatchDisplay().myTimeMs, 150);
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
    EXPECT_EQ(mm->getCurrentMatch()->getHunterPlayerId(), player->getServerPlayerId());
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
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "received-match", 0, true,
                     makeTestUuid(OPPONENT_PLAYER_UUID), makeTestUuid(OPPONENT_TEAM_UUID));
    mm->listenForMatchEvents(cmd);

    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_STREQ(mm->getCurrentMatch()->getMatchId(), "received-match");
    EXPECT_EQ(mm->getCurrentMatch()->getHunterPlayerId(), makeTestUuid(OPPONENT_PLAYER_UUID));
    EXPECT_EQ(mm->getCurrentMatch()->getHunterTeamId(), makeTestUuid(OPPONENT_TEAM_UUID));
    EXPECT_EQ(mm->getCurrentMatch()->getBountyPlayerId(), player->getServerPlayerId());
    EXPECT_EQ(mm->getCurrentMatch()->getBountyTeamId(), player->getRegistrationTeam().id);
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
    mm->setReceivedDrawResult();

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerHunterLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->setHunterDrawTime(350);
    mm->setBountyDrawTime(200);
    mm->setReceivedDrawResult();

    EXPECT_FALSE(mm->didWin());
}

inline void matchManagerBountyWinsWhenFaster(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-3", 0, true);
    mm->listenForMatchEvents(cmd);
    mm->setHunterDrawTime(400);
    mm->setBountyDrawTime(250);
    mm->setReceivedDrawResult();

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-4", 0, true);
    mm->listenForMatchEvents(cmd);
    mm->setHunterDrawTime(150);
    mm->setBountyDrawTime(300);
    mm->setReceivedDrawResult();

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

    mm->clearButtonMasherCount();
    masherCallback(mm);

    mm->setMatchLocalStartTime(1000);
    mm->getMatchButtonPush()(mm);
    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_EQ(mm->getCurrentMatch()->getHunterDrawTime(), 75u);

    masherCallback(mm);
    masherCallback(mm);
    mm->clearCurrentMatch();
    EXPECT_FALSE(mm->getCurrentMatch().has_value());

    mm->initializeMatch(dummyMac);
    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    mm->setMatchLocalStartTime(1000);
    mm->getMatchButtonPush()(mm);
    EXPECT_EQ(mm->getCurrentMatch()->getHunterDrawTime(), 0u);
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

// A fast draw behind a big posse goes below zero. The frame carries the raw
// time and the boost, the record the raw time, the result screen the difference.
TEST_F(MatchManagerTestSuite, boostPastTheRawTimeGoesNegativeAndStillWins) {
    static const uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    setupMatchAsHunter();
    const std::string matchId = matchManager->getCurrentMatch()->getMatchId();
    matchManager->setBoostProvider([]() -> unsigned long { return 60; });
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(45);
    matchManager->getMatchButtonPush()(matchManager);

    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->command, MatchCmd::DRAW_RESULT);
    EXPECT_EQ(radioTap.lastFrame()->playerDrawTime, 45);
    EXPECT_EQ(radioTap.lastFrame()->boostMs, 60u);
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 45u);

    // The bounty drew in 10 ms with nobody behind it: faster raw, slower effective.
    MatchCommand result(opponentMac, MatchCmd::DRAW_RESULT, matchId.c_str(), 10, false);
    matchManager->listenForMatchEvents(result);
    EXPECT_TRUE(matchManager->didWin());

    ASSERT_TRUE(matchManager->finalizeMatch());
    EXPECT_EQ(matchManager->getLastMatchDisplay().myTimeMs, -15);
    EXPECT_EQ(matchManager->getLastMatchDisplay().opponentTimeMs, 10);
}

// An opponent's boost reaches this device only in its frame, so the frame's
// boost has to decide the winner here too, and the record's.
TEST_F(MatchManagerTestSuite, opponentsBoostBeatsAFasterRawDraw) {
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "71111111-2222-4000-8000-333333333333";
    setupMatchAsBounty(matchId);

    MatchPacket frame{};
    frame.command = MatchCmd::DRAW_RESULT;
    IdGenerator::copyId(frame.matchId, matchId);
    frame.isHunter = 1;
    frame.playerDrawTime = 45;
    frame.boostMs = 60;
    frame.seqId = 9;
    radioTap.deliverFrameFrom(hunterMac, &frame);

    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(5);
    matchManager->getMatchButtonPush()(matchManager);
    EXPECT_FALSE(matchManager->didWin()) << "5 ms beat 45 ms less a 60 ms boost";

    ASSERT_TRUE(matchManager->finalizeMatch());
    EXPECT_EQ(matchManager->getLastMatchDisplay().myTimeMs, 5);
    EXPECT_EQ(matchManager->getLastMatchDisplay().opponentTimeMs, -15);
    Match stored = storedMatch(1);
    EXPECT_EQ(stored.getWinnerPlayerId(), makeTestUuid(OPPONENT_PLAYER_UUID))
    << "the boosted hunter won";
    EXPECT_EQ(stored.getHunterDrawTime(), 45u);
    EXPECT_EQ(stored.getBountyDrawTime(), 5u);
}

// With no opponent time there is nothing to be faster than, however far below
// zero the local time went.
TEST_F(MatchManagerTestSuite, negativeTimeDoesNotWinAgainstAResultThatNeverArrived) {
    setupAckedMatchAsHunter();
    matchManager->setBoostProvider([]() -> unsigned long { return 60; });
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(45);
    matchManager->getMatchButtonPush()(matchManager);

    EXPECT_FALSE(matchManager->didWin());
    // The opponent may name another winner; the record keeps this device's.
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    EXPECT_EQ(storedMatch(1).getWinnerPlayerId(), makeTestUuid(OPPONENT_PLAYER_UUID));
}

// A tie is a loss on both screens, and the record names the Bounty.
TEST_F(MatchManagerTestSuite, tiedEffectiveTimesLoseOnScreenAndRecordTheBounty) {
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "a2222222-2222-4000-8000-333333333333";
    setupMatchAsBounty(matchId);
    MatchCommand result(hunterMac, MatchCmd::DRAW_RESULT, matchId, 230, true);
    result.boostMs = 30;
    matchManager->listenForMatchEvents(result);
    matchManager->setBoostProvider([]() -> unsigned long { return 10; });
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(210);
    matchManager->getMatchButtonPush()(matchManager);

    EXPECT_FALSE(matchManager->didWin());
    ASSERT_TRUE(matchManager->finalizeMatch());
    Match stored = storedMatch(1);
    EXPECT_EQ(stored.getWinnerPlayerId(), player->getServerPlayerId());
    EXPECT_EQ(stored.getBountyDrawTime(), 210u);
}

// No frame earns more than a full posse can give.
TEST_F(MatchManagerTestSuite, opponentsBoostIsHeldToAFullPosse) {
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "81111111-2222-4000-8000-333333333333";
    setupMatchAsBounty(matchId);

    MatchCommand tooFast(hunterMac, MatchCmd::DRAW_RESULT, matchId, 300, true);
    tooFast.boostMs = 100000;
    matchManager->listenForMatchEvents(tooFast);
    ASSERT_TRUE(matchManager->finalizeMatch());
    EXPECT_EQ(PosseManager::getMaxBoostMs(), 270u);
    EXPECT_EQ(matchManager->getLastMatchDisplay().opponentTimeMs, 300 - 270);
}

// The sender holds its own boost to the same bound, or the two devices would
// compare different numbers.
TEST_F(MatchManagerTestSuite, localBoostIsHeldToAFullPosse) {
    setupMatchAsHunter();
    matchManager->setBoostProvider([]() -> unsigned long { return 100000; });
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(300);
    matchManager->getMatchButtonPush()(matchManager);

    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->boostMs, 270u);
    ASSERT_TRUE(matchManager->finalizeMatch());
    EXPECT_EQ(matchManager->getLastMatchDisplay().myTimeMs, 300 - 270);
}

// A time below zero is not a reaction time; as an effective time it would
// beat every honest draw.
TEST_F(MatchManagerTestSuite, drawResultWithANegativeTimeIsIgnored) {
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "83333333-2222-4000-8000-333333333333";
    setupMatchAsBounty(matchId);

    MatchCommand negative(hunterMac, MatchCmd::DRAW_RESULT, matchId, -5000, true);
    matchManager->listenForMatchEvents(negative);

    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 0u);
}

// ============================================
// Match record ring
// ============================================

TEST_F(MatchManagerTestSuite, finalizeAppendsTheRecordAndBumpsSeq) {
    fakeClock->setUnixTimeMs(1763200000123LL);
    ASSERT_TRUE(playHunterWin());

    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    Match stored = storedMatch(1);
    EXPECT_EQ(stored.getHunterPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
    EXPECT_EQ(stored.getBountyPlayerId(), makeTestUuid(OPPONENT_PLAYER_UUID));
    EXPECT_EQ(stored.getHunterTeamId(), player->getRegistrationTeam().id);
    EXPECT_EQ(stored.getBountyTeamId(), makeTestUuid(OPPONENT_TEAM_UUID));
    EXPECT_EQ(stored.getWinnerPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
    EXPECT_EQ(stored.getHunterDrawTime(), 200u);
    EXPECT_EQ(stored.getBountyDrawTime(), 300u);
    EXPECT_EQ(stored.getOccurredAtUnixMs(), 1763200000123LL);

    ASSERT_TRUE(playHunterWin());
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 2u);
    uint8_t slot[sizeof(MatchRecord)];
    EXPECT_EQ(storage.readBytes(MATCH_RING_PREFS_NAMESPACE, "match_1", slot, sizeof(slot)),
              sizeof(MatchRecord))
    << "record 2 goes in slot 1";
}

TEST_F(MatchManagerTestSuite, handshakeFramesCarryPlayerAndTeam) {
    setupMatchAsHunter();
    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->command, MatchCmd::SEND_MATCH_ID);
    EXPECT_EQ(radioTap.lastFrame()->playerUuid, player->getServerPlayerId());
    EXPECT_EQ(radioTap.lastFrame()->teamUuid, player->getRegistrationTeam().id);
    matchManager->clearCurrentMatch();

    setupMatchAsBounty();
    EXPECT_EQ(radioTap.lastFrame()->command, MatchCmd::MATCH_ID_ACK);
    EXPECT_EQ(radioTap.lastFrame()->playerUuid, player->getServerPlayerId());
    EXPECT_EQ(radioTap.lastFrame()->teamUuid, player->getRegistrationTeam().id);
}

TEST_F(MatchManagerTestSuite, shootoutMatchIsNotStored) {
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    matchManager->initializeShootoutMatch("SHT-0001000200030004000500060007", opponentMac, true);
    matchManager->getCurrentMatch()->setPlayer(false, makeTestUuid(OPPONENT_PLAYER_UUID),
                                               makeTestUuid(OPPONENT_TEAM_UUID));
    matchManager->setHunterDrawTime(200);
    matchManager->setBountyDrawTime(300);
    matchManager->setReceivedButtonPush();
    matchManager->setReceivedDrawResult();
    ASSERT_TRUE(matchManager->finalizeMatch());
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 0u);
}

// The server rejects or flags these records; the PDN keeps them as computed.
TEST_F(MatchManagerTestSuite, nilOpponentIsStored) {
    ASSERT_TRUE(playHunterWin(UUIDBytes{}));
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    Match stored = storedMatch(1);
    EXPECT_EQ(stored.getHunterPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
    EXPECT_TRUE(stored.getBountyPlayerId().isZero());
    EXPECT_EQ(stored.getWinnerPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
}

TEST_F(MatchManagerTestSuite, unregisteredLocalPlayerIsStored) {
    Player unregistered;
    matchManager->initialize(&unregistered, &storage);
    ASSERT_TRUE(playHunterWin());
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    Match stored = storedMatch(1);
    EXPECT_TRUE(stored.getHunterPlayerId().isZero());
    EXPECT_EQ(stored.getBountyPlayerId(), makeTestUuid(OPPONENT_PLAYER_UUID));
    EXPECT_TRUE(stored.getWinnerPlayerId().isZero());
    matchManager->initialize(player, &storage);
}

TEST_F(MatchManagerTestSuite, samePlayerOnBothSidesIsStored) {
    ASSERT_TRUE(playHunterWin(makeTestUuid(LOCAL_PLAYER_UUID)));
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    Match stored = storedMatch(1);
    EXPECT_EQ(stored.getHunterPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
    EXPECT_EQ(stored.getBountyPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
    EXPECT_EQ(stored.getWinnerPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
}

// Each device names itself the winner when neither pressed.
TEST_F(MatchManagerTestSuite, matchNeitherSidePressedIsStored) {
    setupAckedMatchAsHunter();
    matchManager->sendNeverPressed(4000);
    static const uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand neverPressed(opponentMac, MatchCmd::NEVER_PRESSED,
                              matchManager->getCurrentMatch()->getMatchId(), 4000, false);
    matchManager->listenForMatchEvents(neverPressed);
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    EXPECT_EQ(storedMatch(1).getWinnerPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
}

// A side that never pressed loses on both devices.
TEST_F(MatchManagerTestSuite, matchTheOpponentNeverPressedIsStored) {
    setupAckedMatchAsHunter();
    matchManager->setHunterDrawTime(250);
    matchManager->setReceivedButtonPush();
    matchManager->setOpponentNeverPressed();
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);
    EXPECT_EQ(storedMatch(1).getWinnerPlayerId(), makeTestUuid(LOCAL_PLAYER_UUID));
}

TEST_F(MatchManagerTestSuite, fullRingRefusesTheNextMatch) {
    for (uint32_t i = 0; i < MatchManager::MATCH_RING_CAPACITY; i++) {
        ASSERT_TRUE(playHunterWin());
    }
    uint8_t firstRecord[sizeof(MatchRecord)];
    ASSERT_EQ(
    storage.readBytes(MATCH_RING_PREFS_NAMESPACE, "match_0", firstRecord, sizeof(firstRecord)),
    sizeof(MatchRecord));
    uint32_t pending = 0;
    uint8_t offered[sizeof(MatchRecord)];
    ASSERT_TRUE(matchManager->readLowestPending(pending, offered));
    EXPECT_EQ(pending, 1u) << "with 32 waiting, a newer match went to the FDN first";
    EXPECT_EQ(memcmp(offered, firstRecord, sizeof(MatchRecord)), 0);

    EXPECT_FALSE(playHunterWin()) << "the 33rd unsent match was stored";
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 32u);
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value())
    << "the refusal blocked the next match";
    MatchRecord stillFirst{};
    ASSERT_TRUE(matchManager->readRecord(1, stillFirst));
    EXPECT_EQ(memcmp(&stillFirst, firstRecord, sizeof(MatchRecord)), 0);

    // Once an FDN takes record 1, its slot holds record 33.
    ASSERT_TRUE(matchManager->markCollected(1));
    ASSERT_TRUE(playHunterWin());
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 33u);
    MatchRecord gone{};
    EXPECT_FALSE(matchManager->readRecord(1, gone));
    EXPECT_TRUE(matchManager->readRecord(33, gone));
}

TEST_F(MatchManagerTestSuite, markCollectedMovesTheFirstPendingRecord) {
    uint32_t pending = 0;
    uint8_t record[sizeof(MatchRecord)];
    EXPECT_FALSE(matchManager->readLowestPending(pending, record));
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(playHunterWin());
    }
    ASSERT_TRUE(matchManager->readLowestPending(pending, record));
    EXPECT_EQ(pending, 1u);

    EXPECT_TRUE(matchManager->markCollected(2));
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 2u);
    ASSERT_TRUE(matchManager->readLowestPending(pending, record));
    EXPECT_EQ(pending, 3u);
    EXPECT_FALSE(matchManager->markCollected(2)) << "already collected";
    EXPECT_FALSE(matchManager->markCollected(4)) << "never stored";

    EXPECT_TRUE(matchManager->markCollected(3));
    EXPECT_FALSE(matchManager->readLowestPending(pending, record));
}

// A collected mark above the newest record (#535): whatever the mark is made
// of, the next match is stored, not refused as if 32 waited for an FDN.
TEST_F(MatchManagerTestSuite, markAboveTheNewestRecordStillStoresTheNextMatch) {
    storage.writeUInt(MATCH_RING_PREFS_NAMESPACE, "collectedSeq", 20);

    ASSERT_TRUE(playHunterWin()) << "the stale mark refused the match";
    ASSERT_GT(matchManager->getMatchSequenceNumber(), 0u);
    EXPECT_EQ(storedMatch(matchManager->getMatchSequenceNumber()).getBountyPlayerId(),
              makeTestUuid(OPPONENT_PLAYER_UUID));
}

// The bounty's ack comes off the radio; its player and team fields land in the
// record as the bounty's player and team.
TEST_F(MatchManagerTestSuite, ackFromTheRadioStoresTheBountysPlayerAndTeam) {
    static const uint8_t bountyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    setupMatchAsHunter();
    MatchPacket frame{};
    frame.command = MatchCmd::MATCH_ID_ACK;
    IdGenerator::copyId(frame.matchId, matchManager->getCurrentMatch()->getMatchId());
    frame.playerUuid = makeTestUuid(OPPONENT_PLAYER_UUID);
    frame.teamUuid = makeTestUuid(OPPONENT_TEAM_UUID);
    frame.seqId = 4;
    radioTap.deliverFrameFrom(bountyMac, &frame);
    matchManager->setHunterDrawTime(200);
    matchManager->setBountyDrawTime(300);
    matchManager->setReceivedButtonPush();
    matchManager->setReceivedDrawResult();
    ASSERT_TRUE(matchManager->finalizeMatch());

    Match stored = storedMatch(1);
    EXPECT_EQ(stored.getBountyPlayerId(), makeTestUuid(OPPONENT_PLAYER_UUID));
    EXPECT_EQ(stored.getBountyTeamId(), makeTestUuid(OPPONENT_TEAM_UUID));
}

// Matches stored by older firmware stay byte for byte.
TEST_F(MatchManagerTestSuite, olderFirmwareMatchesAreLeftUntouched) {
    const std::string legacy[3] = {
    R"({"match_id":"a","hunter":"1111","bounty":"2222","winner_is_hunter":true})",
    R"({"match_id":"b","hunter":"3333","bounty":"4444","winner_is_hunter":false})",
    R"({"match_id":"c","hunter":"5555","bounty":"6666","winner_is_hunter":true})"};
    storage.writeUChar(LEGACY_MATCHES_NAMESPACE, "count", 3);
    for (int i = 0; i < 3; i++) {
        storage.write(LEGACY_MATCHES_NAMESPACE, "match_" + std::to_string(i), legacy[i]);
    }

    ASSERT_TRUE(playHunterWin());
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 1u);

    EXPECT_EQ(storage.readUChar(LEGACY_MATCHES_NAMESPACE, "count", 0), 3);
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(storage.read(LEGACY_MATCHES_NAMESPACE, "match_" + std::to_string(i), ""),
                  legacy[i]);
    }
    EXPECT_EQ(storage.read(LEGACY_MATCHES_NAMESPACE, "match_3", "absent"), "absent");
    EXPECT_EQ(storage.readUChar(LEGACY_MATCHES_NAMESPACE, "test_key", 0), 0);
}

// The opponent reports its own slot; a frame naming this device's slot,
// result or never-pressed, changes nothing.
TEST_F(MatchManagerTestSuite, drawResultNamingTheLocalSlotIsIgnored) {
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "91111111-2222-4000-8000-333333333333";
    setupMatchAsBounty(matchId);
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(200);
    matchManager->getMatchButtonPush()(matchManager);

    MatchCommand asBounty(hunterMac, MatchCmd::DRAW_RESULT, matchId, 9000, false);
    matchManager->listenForMatchEvents(asBounty);
    MatchCommand neverAsBounty(hunterMac, MatchCmd::NEVER_PRESSED, matchId, 9000, false);
    matchManager->listenForMatchEvents(neverAsBounty);

    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getBountyDrawTime(), 200u);
    EXPECT_FALSE(matchManager->matchResultsAreIn());
}

// In a shootout the draw slot comes from MAC order, not the standing role: a
// standing Hunter in the Bounty slot takes the Hunter-slot result.
TEST_F(MatchManagerTestSuite, shootoutSlotNotStandingRoleDecidesTheLocalSlot) {
    static const uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "SHT-00000000000000000000000000478";
    player->setIsHunter(true);
    matchManager->initializeShootoutMatch(matchId, mac, false);

    MatchCommand asBounty(opponentMac, MatchCmd::DRAW_RESULT, matchId, 300, false);
    matchManager->listenForMatchEvents(asBounty);
    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());

    MatchCommand asHunter(opponentMac, MatchCmd::DRAW_RESULT, matchId, 250, true);
    matchManager->listenForMatchEvents(asHunter);
    EXPECT_TRUE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 250u);
}
