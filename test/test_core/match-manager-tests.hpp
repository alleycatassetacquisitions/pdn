#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "alleycat-server/fdn-record-store.hpp"
#include "wireless/record-relay-types.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "game/shootout-manager.hpp"
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

        player = new Player();
        char playerId[] = "test";
        player->setUserID(playerId);
        rebuildMatchManager(&mockStorage);
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
    }

    /**
     * Replaces the manager with a fresh one on `storage`, which must outlive it
     * (~MatchManager calls storage->end()). No active match or boost provider
     * carries over.
     */
    void rebuildMatchManager(StorageInterface* storage) {
        // Old manager first: a match channel built while it stands is refused the
        // kMatchCommand type.
        delete matchManager;
        matchManager = new MatchManager(player, storage, device.wirelessManager, &fakeRdc);
    }

    /**
     * Allocates the in-memory prefs a test keeps real match records in. One per
     * test; the fixture frees it once the manager built on it is gone.
     */
    NativePrefsDriver& makeNativePrefs(const char* name) {
        nativePrefs = new NativePrefsDriver(name, {MATCHES_PREFS_NAMESPACE});
        return *nativePrefs;
    }

    /**
     * Reads the match stored under sequence number @p seq from the prefs
     * makeNativePrefs() allocated. False when its ring slot holds no record.
     */
    bool readStoredMatch(uint32_t seq, Match& out) {
        char key[16];
        snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX,
                 static_cast<unsigned>((seq - 1) % MATCH_RING_CAPACITY));
        uint8_t buffer[MATCH_BINARY_SIZE] = {};
        if (nativePrefs->readBytes(MATCHES_PREFS_NAMESPACE, key, buffer, sizeof(buffer)) !=
            MATCH_BINARY_SIZE) {
            return false;
        }
        out.deserialize(buffer);
        return true;
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        // After the manager, whose destructor closes the storage it was built on.
        delete nativePrefs;
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

    MatchManager* matchManager = nullptr;
    Player* player;
    MockDevice device;
    MockStorage mockStorage;
    NativePrefsDriver* nativePrefs = nullptr;
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
    MatchCommand result(dummyMac, MatchCmd::DRAW_RESULT, mm->getCurrentMatch()->getMatchId(), 300,
                        false);
    mm->listenForMatchEvents(result);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerHunterLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->setHunterDrawTime(350);
    MatchCommand result(dummyMac, MatchCmd::DRAW_RESULT, mm->getCurrentMatch()->getMatchId(), 200,
                        false);
    mm->listenForMatchEvents(result);

    EXPECT_FALSE(mm->didWin());
}

inline void matchManagerBountyWinsWhenFaster(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-3", 0, true);
    mm->listenForMatchEvents(cmd);
    MatchCommand result(dummyMac, MatchCmd::DRAW_RESULT, "match-3", 400, true);
    mm->listenForMatchEvents(result);
    mm->setBountyDrawTime(250);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-4", 0, true);
    mm->listenForMatchEvents(cmd);
    MatchCommand result(dummyMac, MatchCmd::DRAW_RESULT, "match-4", 150, true);
    mm->listenForMatchEvents(result);
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

// One match at a time. An initiation that arrives during a match gets nothing
// back, not even the role-mismatch a same-role sender would otherwise be owed.
inline void matchManagerIgnoresAnInitiationDuringAMatch(MatchManagerTestSuite* suite) {
    suite->setupMatchAsHunter();
    const size_t sentBefore = suite->radioTap.sentFrames.size();

    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand second(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-xyz", 0, true);
    suite->matchManager->listenForMatchEvents(second);

    EXPECT_EQ(suite->radioTap.sentFrames.size(), sentBefore)
    << "answered an initiation while already in a match";
}

// A tournament pairs its own players.
inline void matchManagerIgnoresAnInitiationDuringATournament(MatchManagerTestSuite* suite) {
    suite->player->setIsHunter(false);
    ShootoutManager shootout(suite->player, suite->device.wirelessManager, &suite->fakeRdc,
                             suite->matchManager);
    shootout.startProposal();
    ASSERT_TRUE(shootout.active());

    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-abc", 0, true);
    suite->matchManager->listenForMatchEvents(cmd);
    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());

    // And accepted again once the tournament is over.
    shootout.resetToIdle();
    suite->matchManager->listenForMatchEvents(cmd);
    EXPECT_TRUE(suite->matchManager->getCurrentMatch().has_value());
}

// No delivery report ever came, so the hunter may never have heard the ack.
inline void matchManagerBountyDropsTheMatchWhenItsAckIsAbandoned(MatchManagerTestSuite* suite) {
    suite->player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand cmd(dummyMac, MatchCmd::SEND_MATCH_ID, "match-id-abc", 0, true);
    suite->matchManager->listenForMatchEvents(cmd);
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    suite->fakeClock->advance(Resender::RETRANSMIT_SPAN_MS + 1);
    suite->device.wirelessManager->sync();

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
    device.wirelessManager->sync();
    const size_t beforeClear = sentCommands.size();
    ASSERT_GT(beforeClear, 1u) << "the handshake never retransmitted";

    matchManager->clearCurrentMatch();
    fakeClock->advance(Resender::INITIAL_TIMEOUT_MS * 2 + 1);
    device.wirelessManager->sync();

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
    device.wirelessManager->sync();

    bool resultRetried = false;
    for (size_t i = afterResult; i < sentCommands.size(); ++i) {
        if (sentCommands[i] == MatchCmd::DRAW_RESULT) resultRetried = true;
    }
    EXPECT_TRUE(resultRetried)
        << "clearing a finished match stripped the retransmits of its DRAW_RESULT";
}

TEST_F(MatchManagerTestSuite, ringBufferStores32MatchesAndWrapsAtSlot0) {
    NativePrefsDriver& prefs = makeNativePrefs("ring-prefs");
    rebuildMatchManager(&prefs);

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

    uint32_t oldestPending = 0;
    MatchRecordContent oldest{};
    ASSERT_TRUE(matchManager->readLowestPending(oldestPending, oldest));
    EXPECT_EQ(oldestPending, 2u) << "match 1 was overwritten by match 33";

    uint8_t slot0[MATCH_BINARY_SIZE] = {};
    ASSERT_EQ(prefs.readBytes(MATCHES_PREFS_NAMESPACE, "match_0", slot0, MATCH_BINARY_SIZE),
              MATCH_BINARY_SIZE);
    Match slot0Match{};
    slot0Match.deserialize(slot0);
    EXPECT_STREQ(slot0Match.getMatchId(), "00000033-1234-4000-8000-000000000000");
}

TEST_F(MatchManagerTestSuite, initializeDropsRecordsFromBeforeTheRing) {
    NativePrefsDriver& prefs = makeNativePrefs("legacy-prefs");
    prefs.write(MATCHES_PREFS_NAMESPACE, "match_40", R"({"match_id":"old"})");
    prefs.writeUChar(MATCHES_PREFS_NAMESPACE, "count", 41);

    rebuildMatchManager(&prefs);

    EXPECT_EQ(prefs.read(MATCHES_PREFS_NAMESPACE, "match_40", ""), "");
    EXPECT_EQ(prefs.readUChar(MATCHES_PREFS_NAMESPACE, "count", 0), 0);
}

TEST_F(MatchManagerTestSuite, sendMatchIdRejectsSelfPlayerUuidButAcceptsDemoOpponent) {
    NativePrefsDriver& prefs = makeNativePrefs("demo-prefs");
    rebuildMatchManager(&prefs);
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
    NativePrefsDriver& prefs = makeNativePrefs("winner-prefs");
    rebuildMatchManager(&prefs);
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
    ASSERT_TRUE(readStoredMatch(1, onBountyDevice));
    ASSERT_TRUE(readStoredMatch(2, onHunterDevice));
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
    NativePrefsDriver& prefs = makeNativePrefs("never-prefs");
    rebuildMatchManager(&prefs);
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    Match stored{};

    setupMatchAsBounty("61111111-2222-4000-8000-333333333333");
    matchManager->setHunterDrawTime(5000);
    matchManager->setBountyDrawTime(1000);
    matchManager->setReceivedDrawResult();
    matchManager->setNeverPressed();
    ASSERT_FALSE(matchManager->didWin());
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_TRUE(readStoredMatch(1, stored));
    EXPECT_EQ(stored.getWinnerPlayerId(), stored.getHunterPlayerId());

    setupMatchAsBounty("62222222-2222-4000-8000-333333333333");
    matchManager->setBountyDrawTime(5000);
    matchManager->setReceivedButtonPush();
    MatchCommand neverPressed(hunterMac, MatchCmd::NEVER_PRESSED,
                              "62222222-2222-4000-8000-333333333333", 1000, true);
    matchManager->listenForMatchEvents(neverPressed);
    ASSERT_TRUE(matchManager->didWin());
    ASSERT_TRUE(matchManager->finalizeMatch());
    ASSERT_TRUE(readStoredMatch(2, stored));
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
    NativePrefsDriver& prefs = makeNativePrefs("negative-prefs");
    rebuildMatchManager(&prefs);
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
    Match stored{};
    ASSERT_TRUE(readStoredMatch(1, stored));
    EXPECT_EQ(stored.getWinnerPlayerId(), stored.getHunterPlayerId());
    EXPECT_EQ(stored.getHunterDrawTime(), 45u);
    EXPECT_EQ(stored.getBountyDrawTime(), 5u);
}

// With no opponent time there is nothing to be faster than, however far below
// zero the local time went.
TEST_F(MatchManagerTestSuite, negativeTimeDoesNotWinAgainstAResultThatNeverArrived) {
    setupMatchAsHunter();
    matchManager->setBoostProvider([]() -> unsigned long { return 60; });
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(45);
    matchManager->getMatchButtonPush()(matchManager);

    EXPECT_FALSE(matchManager->didWin());
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
    EXPECT_EQ(matchManager->getLastMatchDisplay().opponentTimeMs,
              300 - static_cast<long>(PosseManager::getMaxBoostMs()));
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
    EXPECT_EQ(radioTap.lastFrame()->boostMs, PosseManager::getMaxBoostMs());
    ASSERT_TRUE(matchManager->finalizeMatch());
    EXPECT_EQ(matchManager->getLastMatchDisplay().myTimeMs,
              300 - static_cast<long>(PosseManager::getMaxBoostMs()));
}

// A time below zero is not a reaction time; as an effective time it could
// beat any honest draw.
TEST_F(MatchManagerTestSuite, drawResultWithANegativeTimeIsIgnored) {
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "83333333-2222-4000-8000-333333333333";
    setupMatchAsBounty(matchId);

    MatchCommand negative(hunterMac, MatchCmd::DRAW_RESULT, matchId, -5000, true);
    matchManager->listenForMatchEvents(negative);

    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 0u);
}

// The opponent reports its own slot. A result naming this device's slot would
// replace the local time with whatever the frame says.
TEST_F(MatchManagerTestSuite, drawResultNamingTheLocalSlotIsIgnored) {
    static const uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "91111111-2222-4000-8000-333333333333";
    setupMatchAsBounty(matchId);
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(200);
    matchManager->getMatchButtonPush()(matchManager);

    MatchCommand asBounty(hunterMac, MatchCmd::DRAW_RESULT, matchId, 9000, false);
    matchManager->listenForMatchEvents(asBounty);

    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getBountyDrawTime(), 200u);
}

TEST_F(MatchManagerTestSuite, collectedHighWaterMarkClampsWhenCorruptedAboveSeq) {
    NativePrefsDriver& prefs = makeNativePrefs("clamp-prefs");
    prefs.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, 5u);
    prefs.writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 99u);
    rebuildMatchManager(&prefs);

    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 5u);
    EXPECT_EQ(prefs.readUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0u), 5u)
    << "the clamp must persist, or matches 6..99 would read as collected once stored";
}

// The match store as the record relay reads it: the lowest pending record and
// the mark, nothing else.
TEST_F(MatchManagerTestSuite, pendingMatchRecordsRunFromTheCollectedMarkToTheNewestMatch) {
    NativePrefsDriver& prefs = makeNativePrefs("source-prefs");
    rebuildMatchManager(&prefs);

    uint32_t recordNumber = 99;
    MatchRecordContent content;
    memset(&content, 0xEE, sizeof(content));
    EXPECT_FALSE(matchManager->readLowestPending(recordNumber, content)) << "no match is stored";
    EXPECT_EQ(recordNumber, 99u) << "nothing pending must leave the outputs alone";
    EXPECT_EQ(content.recordBytes[0], 0xEE);
    EXPECT_EQ(content.tag[0], 0xEE);

    for (uint32_t i = 1; i <= 3; ++i) {
        char mid[37];
        snprintf(mid, sizeof(mid), "%08u-5678-4000-8000-000000000000", i);
        setupMatchAsBounty(mid);
        matchManager->setHunterDrawTime(180);
        matchManager->setBountyDrawTime(120);
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        ASSERT_TRUE(matchManager->finalizeMatch());
    }

    ASSERT_TRUE(matchManager->readLowestPending(recordNumber, content));
    EXPECT_EQ(recordNumber, 1u);
    uint8_t stored[MATCH_BINARY_SIZE] = {};
    ASSERT_EQ(prefs.readBytes(MATCHES_PREFS_NAMESPACE, "match_0", stored, sizeof(stored)),
              MATCH_BINARY_SIZE);
    EXPECT_EQ(memcmp(content.recordBytes, stored, sizeof(stored)), 0)
    << "the record bytes are the 112 bytes the PDN stored";
    for (size_t i = 0; i < sizeof(content.tag); ++i) {
        ASSERT_EQ(content.tag[i], 0) << "tag byte " << i << " is not zero";
    }
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 0u) << "reading a record retired it";

    matchManager->advanceMark(2);
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 2u);
    EXPECT_EQ(prefs.readUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0u), 2u)
    << "the mark the relay advances is the stored collected_high_water_mark";
    ASSERT_TRUE(matchManager->readLowestPending(recordNumber, content));
    EXPECT_EQ(recordNumber, 3u);

    matchManager->advanceMark(1);
    matchManager->advanceMark(0);
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 2u) << "the mark moved backwards";
    matchManager->advanceMark(4);
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 2u)
    << "the mark passed the newest stored match";

    matchManager->advanceMark(3);
    EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 3u);
    recordNumber = 99;
    EXPECT_FALSE(matchManager->readLowestPending(recordNumber, content));
    EXPECT_EQ(recordNumber, 99u);
}

// Pending starts at max(collected_high_water_mark + 1, match_sequence_number - 31):
// a match the 32-slot ring has overwritten is gone.
TEST_F(MatchManagerTestSuite, pendingMatchRecordsStartAtTheOldestMatchStillInTheRing) {
    NativePrefsDriver& prefs = makeNativePrefs("source-ring-prefs");
    rebuildMatchManager(&prefs);
    for (uint32_t i = 1; i <= 34; ++i) {
        char mid[37];
        snprintf(mid, sizeof(mid), "%08u-9abc-4000-8000-000000000000", i);
        setupMatchAsBounty(mid);
        matchManager->setHunterDrawTime(180);
        matchManager->setBountyDrawTime(120);
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        ASSERT_TRUE(matchManager->finalizeMatch());
    }
    ASSERT_EQ(matchManager->getMatchSequenceNumber(), 34u);
    ASSERT_EQ(matchManager->getCollectedHighWaterMark(), 0u);

    uint32_t recordNumber = 0;
    MatchRecordContent content{};
    ASSERT_TRUE(matchManager->readLowestPending(recordNumber, content));
    EXPECT_EQ(recordNumber, 3u) << "matches 1 and 2 were overwritten; 34 - 31 is the oldest held";
    Match oldest{};
    oldest.deserialize(content.recordBytes);
    EXPECT_STREQ(oldest.getMatchId(), "00000003-9abc-4000-8000-000000000000");

    matchManager->advanceMark(10);
    ASSERT_TRUE(matchManager->readLowestPending(recordNumber, content));
    EXPECT_EQ(recordNumber, 11u) << "inside the ring the mark decides";
}

// Nothing readable is left to offer, and nothing may be retired to get past it.
TEST_F(MatchManagerTestSuite, pendingMatchRecordsThatAreAllUnreadableOfferNothingAndKeepTheMark) {
    NativePrefsDriver& prefs = makeNativePrefs("source-torn-prefs");
    rebuildMatchManager(&prefs);
    for (uint32_t i = 1; i <= 3; ++i) {
        char mid[37];
        snprintf(mid, sizeof(mid), "%08u-def0-4000-8000-000000000000", i);
        setupMatchAsBounty(mid);
        matchManager->setHunterDrawTime(180);
        matchManager->setBountyDrawTime(120);
        matchManager->setReceivedButtonPush();
        matchManager->setReceivedDrawResult();
        ASSERT_TRUE(matchManager->finalizeMatch());
    }
    matchManager->advanceMark(1);
    ASSERT_EQ(matchManager->getCollectedHighWaterMark(), 1u);
    // Matches 2 and 3, every record still pending, are torn.
    const uint8_t torn[3] = {1, 2, 3};
    prefs.writeBytes(MATCHES_PREFS_NAMESPACE, "match_1", torn, sizeof(torn));
    prefs.writeBytes(MATCHES_PREFS_NAMESPACE, "match_2", torn, sizeof(torn));

    uint32_t recordNumber = 99;
    uint8_t untouched[sizeof(MatchRecordContent)];
    memset(untouched, 0xEE, sizeof(untouched));
    MatchRecordContent content;
    memset(&content, 0xEE, sizeof(content));
    // Twice: a first read that retired what it passed over would change the second.
    for (int attempt = 0; attempt < 2; ++attempt) {
        EXPECT_FALSE(matchManager->readLowestPending(recordNumber, content));
        EXPECT_EQ(recordNumber, 99u) << "the output number was written with nothing to offer";
        EXPECT_EQ(memcmp(&content, untouched, sizeof(content)), 0)
        << "the output record or tag was written with nothing to offer";
        EXPECT_EQ(matchManager->getCollectedHighWaterMark(), 1u)
        << "an unreadable record was retired without an acknowledgement";
        EXPECT_EQ(prefs.readUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0u), 1u);
    }
    EXPECT_EQ(matchManager->getMatchSequenceNumber(), 3u);
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

// The record relay stops its sends on this, so a way of starting a match that
// skipped it would let a record retry out during that match.
inline void matchManagerTellsObserversOnceHoweverAMatchStarts(MatchManagerTestSuite* suite) {
    int started = 0;
    suite->matchManager->setOnMatchStarted([&started]() { started++; }, &started);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    suite->player->setIsHunter(true);
    suite->matchManager->initializeMatch(opponentMac);
    EXPECT_EQ(started, 1) << "initializeMatch";
    suite->matchManager->clearCurrentMatch();

    suite->matchManager->initializeShootoutMatch("SHT-observer", opponentMac, false);
    EXPECT_EQ(started, 2) << "initializeShootoutMatch";
    suite->matchManager->clearCurrentMatch();

    suite->matchManager->receiveMatch("observer-match-id", false, opponentMac);
    EXPECT_EQ(started, 3) << "receiveMatch";
    suite->matchManager->clearCurrentMatch();

    suite->matchManager->setOnMatchStarted(nullptr, &started);
    suite->matchManager->initializeShootoutMatch("SHT-unobserved", opponentMac, false);
    EXPECT_EQ(started, 3) << "a removed observer was told";
}
