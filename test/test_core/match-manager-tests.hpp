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
        matchManager->initialize(player, &mockStorage, &wireless.transport);
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
        QuickdrawPacket cmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, matchId, hunterId, 0, true);
        matchManager->listenForMatchEvents(dummyMac, cmd);
    }

    MatchManager* matchManager;
    Player* player;
    MockStorage mockStorage;
    QuickdrawTestStack wireless;
    FakeRemoteDeviceCoordinator fakeRdc;
    FakePlatformClock* fakeClock;
};

// clearCurrentMatch must CANCEL the in-flight reliable sends to the departing
// opponent, not let them run to abandonment: an abandon firing after the clear
// would void the next match primed against the same peer.
inline void matchManagerClearCancelsInflightWithoutAbandon(MatchManagerTestSuite* suite) {
    auto* mm = suite->matchManager;
    suite->setupMatchAsHunter();  // SEND_MATCH_ID now riding the reliable channel
    ASSERT_FALSE(suite->wireless.sent().empty());
    mm->clearCurrentMatch();

    // Fresh match against the same peer, with a committed press: the state a
    // stale abandon would wrongly void.
    uint8_t sameMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeShootoutMatch("SHT-0000000000000000000000000000cncl", sameMac, true);
    mm->setReceivedButtonPush();

    // Drive the resender far past the full retry budget.
    for (int i = 0; i < 8; ++i) {
        suite->fakeClock->advance(2000);
        suite->wireless.transport.sync();
    }

    EXPECT_FALSE(mm->isVoided());
    EXPECT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_EQ(suite->wireless.transport
                  .getStats(PktType::kQuickdrawCommand).abandons, 0u);
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

// Chain boost must never apply inside a shootout match: a ring has no
// supporters, but a chain closed into a ring without unplugging leaves stale
// confirmedSupporters on the ex-champion, and the provider would hand its
// duelist a head start (observed on hardware: boost 30 in a tournament match).
inline void matchManagerShootoutMatchIgnoresChainBoost(MatchManagerTestSuite* suite) {
    auto* mm = suite->matchManager;
    suite->player->setIsHunter(true);
    uint8_t opp[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeShootoutMatch("SHT-0000000000000000000000000000bost", opp, true);
    mm->setBoostProvider([]() -> unsigned long { return 30; });

    suite->fakeClock->setTime(10000);
    mm->setDuelLocalStartTime(10000);
    suite->fakeClock->setTime(10212);  // 212ms reaction

    mm->getDuelButtonPush()(mm);

    EXPECT_EQ(mm->getCurrentMatch()->getHunterDrawTime(), 212u)
        << "shootout press must store the raw time, not chain-boosted";
    EXPECT_EQ(mm->getLastMatchDisplay().boostMs, 0u)
        << "result screen must not show a boost for a shootout match";
}

// Shootout matches (SHT- prefixed) are venue-local: they never upload, so they
// must not feed lifetime reaction-time stats. A normal match still does. This
// guards the same gate finalizeMatch() uses for persistence.
inline void matchManagerShootoutMatchExcludedFromReactionStats(MatchManagerTestSuite* suite) {
    auto* mm = suite->matchManager;
    auto* player = suite->player;
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    mm->initializeShootoutMatch("SHT-00000000000000000000000000000001", dummyMac, true);
    EXPECT_TRUE(mm->currentMatchIsShootout());

    suite->fakeClock->setTime(10000);
    mm->setDuelLocalStartTime(10000);
    suite->fakeClock->setTime(10200);  // 200ms reaction
    mm->getDuelButtonPush()(mm);

    EXPECT_EQ(player->getLastReactionTime(), 0u);  // shootout press recorded nothing

    mm->clearCurrentMatch();

    mm->initializeMatch(dummyMac);
    EXPECT_FALSE(mm->currentMatchIsShootout());
    suite->fakeClock->setTime(20000);
    mm->setDuelLocalStartTime(20000);
    suite->fakeClock->setTime(20150);  // 150ms reaction
    mm->getDuelButtonPush()(mm);

    EXPECT_EQ(player->getLastReactionTime(), 150u);  // normal match still counts
}

// Root-cause guard for the shootout role coupling: the per-match draw-slot is
// carried by the match (localIsHunterForMatch), independent of the global
// hunter/bounty role. A hunter assigned the bounty slot for one shootout match
// writes the bounty draw time and never has player->isHunter() mutated.
inline void matchManagerShootoutMatchRoleDecoupledFromGlobalRole(MatchManagerTestSuite* suite) {
    auto* mm = suite->matchManager;
    auto* player = suite->player;
    player->setIsHunter(true);  // global role: hunter
    uint8_t opp[6] = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02};

    // MAC-ordered slot for this match is BOUNTY, despite the hunter global role.
    mm->initializeShootoutMatch("SHT-00000000000000000000000000000002", opp, false);

    EXPECT_FALSE(mm->localIsHunterForMatch());  // per-match slot = bounty
    EXPECT_TRUE(player->isHunter());            // global role untouched

    suite->fakeClock->setTime(10000);
    mm->setDuelLocalStartTime(10000);
    suite->fakeClock->setTime(10150);  // 150ms reaction
    mm->getDuelButtonPush()(mm);

    // The press lands in the bounty slot, driven by the match role, not the global role.
    EXPECT_EQ(mm->getCurrentMatch()->getBountyDrawTime(), 150u);
    EXPECT_EQ(mm->getCurrentMatch()->getHunterDrawTime(), 0u);
    EXPECT_TRUE(player->isHunter());  // still untouched after the press
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
    QuickdrawPacket cmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, "received-match", "hunt", 0, true);
    mm->listenForMatchEvents(dummyMac, cmd);

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
    QuickdrawPacket cmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, "duel-3", "hunt", 0, true);
    mm->listenForMatchEvents(dummyMac, cmd);
    mm->setHunterDrawTime(400);
    mm->setBountyDrawTime(250);

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyLosesWhenSlower(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawPacket cmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, "duel-4", "hunt", 0, true);
    mm->listenForMatchEvents(dummyMac, cmd);
    mm->setHunterDrawTime(150);
    mm->setBountyDrawTime(300);

    EXPECT_FALSE(mm->didWin());
}

inline void matchManagerHunterWinsWhenBountyNeverPressed(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->setHunterDrawTime(250);
    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::NEVER_PRESSED, mm->getCurrentMatch()->getMatchId(), "boun", 0, false));

    EXPECT_TRUE(mm->didWin());
}

inline void matchManagerBountyWinsWhenHunterNeverPressed(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawPacket cmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, "duel-6", "hunt", 0, true);
    mm->listenForMatchEvents(dummyMac, cmd);
    mm->setBountyDrawTime(300);
    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::NEVER_PRESSED, "duel-6", "hunt", 0, true));

    EXPECT_TRUE(mm->didWin());
}

// First-writer-wins on the opponent's outcome (spec rule OpponentResultReceived,
// requires their_result = null): once a DRAW_RESULT has resolved the opponent's
// side, a later NEVER_PRESSED from the same opponent must not re-resolve it. A
// stale or racing timeout packet cannot turn a loss into a win.
inline void matchManagerOpponentResultIsFirstWriterWins(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    // We pressed slow (350); the opponent's DRAW_RESULT says they pressed fast
    // (200), so we lose.
    mm->setHunterDrawTime(350);
    mm->setReceivedButtonPush();
    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, mm->getCurrentMatch()->getMatchId(), "boun", 200, false));
    ASSERT_TRUE(mm->matchResultsAreIn());
    ASSERT_FALSE(mm->didWin());

    // A late NEVER_PRESSED from the same opponent must NOT flip the resolved loss
    // into a win.
    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::NEVER_PRESSED, mm->getCurrentMatch()->getMatchId(), "boun", 0, false));
    EXPECT_FALSE(mm->didWin());
}

// First-writer-wins also protects the recorded draw TIME, not just their_result.
// We win on a both-pressed time comparison; a late NEVER_PRESSED (pity time 0)
// from the same opponent must not clobber their recorded time to 0, which would
// flip the win into a loss. Guards against the draw-time write escaping the
// their_result == null gate (spec rule OpponentResultReceived).
inline void matchManagerLateTimeoutDoesNotClobberWinningTime(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    // We pressed fast (200); the opponent's DRAW_RESULT says they pressed slow
    // (300), so we win the time comparison.
    mm->setHunterDrawTime(200);
    mm->setReceivedButtonPush();
    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, mm->getCurrentMatch()->getMatchId(), "boun", 300, false));
    ASSERT_TRUE(mm->matchResultsAreIn());
    ASSERT_TRUE(mm->didWin());

    // A late NEVER_PRESSED (carries draw time 0) from the same opponent must not
    // overwrite their recorded 300ms with 0, which would make them "faster".
    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::NEVER_PRESSED, mm->getCurrentMatch()->getMatchId(), "boun", 0, false));
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

    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, mm->getCurrentMatch()->getMatchId(), "boun", 0, false));
    EXPECT_TRUE(mm->getHasReceivedDrawResult());
    EXPECT_TRUE(mm->matchResultsAreIn());
}

// Commands that mutate an active match must only be accepted from the
// match's established opponent (right MAC + right matchId). A neighbor
// device forging results for someone else's duel must be ignored.

// NEVER_PRESSED forged by a non-opponent MAC must not resolve the opponent's
// side as a no-show (which would otherwise force didWin() to true).
inline void matchManagerRejectsNeverPressedFromStranger(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);
    const char* realMatchId = mm->getCurrentMatch()->getMatchId();

    mm->setHunterDrawTime(300);
    mm->setReceivedButtonPush();

    uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    QuickdrawPacket forged = makeQuickdrawPacket(QDCommand::NEVER_PRESSED, realMatchId, "xxxx", 0, false);
    mm->listenForMatchEvents(strangerMac, forged);

    EXPECT_FALSE(mm->didWin());
    EXPECT_FALSE(mm->matchResultsAreIn());
}

// Commands with the wrong matchId from the right MAC must also be rejected
// (prevents stale results from a prior match confusing the current one).
inline void matchManagerRejectsWrongMatchId(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);

    QuickdrawPacket stale = makeQuickdrawPacket(QDCommand::DRAW_RESULT, "wrong-match-id-xxxxxxxxxxxxxxxxxxxx", "opp_", 222, false);
    mm->listenForMatchEvents(opponentMac, stale);

    EXPECT_EQ(mm->getCurrentMatch()->getBountyDrawTime(), 0u);
    EXPECT_FALSE(mm->getHasReceivedDrawResult());
}

// SEND_MATCH_ID from a MAC that isn't an RDC direct peer must be rejected.
// The fixture stubs INPUT_JACK peer to 0x01,0x02,...; this test sends from a
// different MAC and verifies the gate refuses it.
inline void matchManagerSendMatchIdRejectsUnknownPeer(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    QuickdrawPacket cmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, "match-id-000000000000000000000000000", "xxxx", 0, true);
    mm->listenForMatchEvents(strangerMac, cmd);

    EXPECT_FALSE(mm->getCurrentMatch().has_value());
}

// Legitimate path: command with correct MAC + matchId is accepted.
inline void matchManagerAcceptsResultFromOpponent(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);
    const char* realMatchId = mm->getCurrentMatch()->getMatchId();

    QuickdrawPacket legit = makeQuickdrawPacket(QDCommand::DRAW_RESULT, realMatchId, "opp_", 150, false);
    mm->listenForMatchEvents(opponentMac, legit);

    EXPECT_EQ(mm->getCurrentMatch()->getBountyDrawTime(), 150u);
    EXPECT_TRUE(mm->getHasReceivedDrawResult());
}

inline void matchManagerGracePeriodPath(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    mm->setReceivedButtonPush();
    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::NEVER_PRESSED, mm->getCurrentMatch()->getMatchId(), "boun", 0, false));

    EXPECT_TRUE(mm->matchResultsAreIn());
}

// If our grace expired and the opponent's NEVER_PRESSED never arrived, we
// must still finalize from our own state alone. Otherwise both devices
// deadlock waiting for each other.
inline void matchManagerGraceExpiredAloneFinalizes(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    // Grace expired on our side; opponent's NEVER_PRESSED dropped; we never
    // pressed. Only our own timed-out result (myResult, not pressed) unsticks this.
    mm->sendNeverPressed(1500);

    EXPECT_TRUE(mm->matchResultsAreIn());
}

// Voided matches expose the voided flag on the Match object so the upload
// payload carries it; the server uses this to detect packet-loss patterns.
inline void matchManagerVoidedDuelPersistsWithFlag(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    mm->setReceivedButtonPush();
    mm->voidCurrentMatch();

    EXPECT_TRUE(mm->isVoided());
    EXPECT_TRUE(mm->matchResultsAreIn());
    EXPECT_FALSE(mm->didWin());

    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_TRUE(mm->getCurrentMatch()->isVoided());

    std::string json = mm->getCurrentMatch()->toJson();
    EXPECT_NE(json.find("\"voided\":true"), std::string::npos);
    EXPECT_NE(json.find("\"winner_is_hunter\""), std::string::npos);
}

// The non-presser locally knows they lose. A NEVER_PRESSED abandon
// must preserve that loss instead of overwriting with void.
inline void matchManagerNeverPressedAbandonPreservesLoss(MatchManager* mm, Player* player) {
    player->setIsHunter(false);  // bounty, the non-presser
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawPacket sendMatchId = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID,
                                  "test-match-abandon-np", "hunt", 0, true);
    mm->listenForMatchEvents(opponentMac, sendMatchId);

    mm->sendNeverPressed(1500);
    mm->onReliableSendAbandoned(opponentMac);

    EXPECT_FALSE(mm->isVoided());
    EXPECT_TRUE(mm->matchResultsAreIn());
    EXPECT_FALSE(mm->didWin());
}

// A DRAW_RESULT abandon means we pressed but the opponent never learned
// our time, so we can't compute against their unknown state. Void.
inline void matchManagerDrawResultAbandonVoids(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);
    mm->setReceivedButtonPush();

    mm->onReliableSendAbandoned(opponentMac);

    EXPECT_TRUE(mm->isVoided());
    EXPECT_TRUE(mm->matchResultsAreIn());
    EXPECT_FALSE(mm->didWin());
    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    EXPECT_TRUE(mm->getCurrentMatch()->isVoided());
}

// A stale abandon for a peer that is no longer our opponent must not void the
// match freshly primed against a different peer; only the active opponent's
// abandon voids.
inline void matchManagerStaleAbandonForOtherPeerIgnored(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t otherMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    mm->initializeMatch(opponentMac);
    mm->setReceivedButtonPush();

    mm->onReliableSendAbandoned(otherMac);
    EXPECT_FALSE(mm->isVoided());

    mm->onReliableSendAbandoned(opponentMac);
    EXPECT_TRUE(mm->isVoided());
}

// A setup-phase abandon (SEND_MATCH_ID never acked) on a not-yet-ready match
// with no committed result drops the half-formed match so Idle re-initiates.
inline void matchManagerSetupAbandonClearsUnreadyMatch(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(opponentMac);
    ASSERT_TRUE(mm->getCurrentMatch().has_value());
    ASSERT_FALSE(mm->isMatchReady());          // no MATCH_ID_ACK yet
    ASSERT_FALSE(mm->getHasPressedButton());

    mm->onReliableSendAbandoned(opponentMac);

    EXPECT_FALSE(mm->getCurrentMatch().has_value())
        << "unacked setup handshake should clear the half-formed match";
}

inline void matchManagerClearMatchResetsState(MatchManager* mm, Player* player) {
    player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);
    mm->setReceivedButtonPush();
    mm->listenForMatchEvents(dummyMac, makeQuickdrawPacket(QDCommand::DRAW_RESULT, mm->getCurrentMatch()->getMatchId(), "boun", 0, false));
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

// initializeMatch zeroes buttonMasherCount, so a masher penalty racked up in
// one duel must not bleed into the next match's reaction time.
inline void matchManagerClearCurrentMatchResetsMasherCount(MatchManagerTestSuite* suite) {
    MatchManager* mm = suite->matchManager;
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    mm->initializeMatch(dummyMac);

    // Rack up a 3-press masher penalty in the first match.
    auto masher = mm->getButtonMasher();
    masher(mm);
    masher(mm);
    masher(mm);

    mm->clearCurrentMatch();
    mm->initializeMatch(dummyMac);

    // A press 200ms after the draw in the fresh match must read 200ms exactly,
    // with no carried-over 3*75ms penalty. No boost provider set, so the stored
    // hunter draw time equals the raw reaction time.
    suite->fakeClock->setTime(10000);
    mm->setDuelLocalStartTime(10000);
    suite->fakeClock->setTime(10200);
    auto press = mm->getDuelButtonPush();
    press(mm);

    EXPECT_EQ(mm->getCurrentMatch()->getHunterDrawTime(), 200u);
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
    QuickdrawPacket ack = makeQuickdrawPacket(QDCommand::MATCH_ID_ACK, matchId, "boun", 0, false);
    mm->listenForMatchEvents(dummyMac, ack);

    EXPECT_TRUE(mm->isMatchReady());
}

// Bounty's matchIsReady becomes true immediately upon receiving SEND_MATCH_ID.
inline void matchManagerBountyMatchIsReadyAfterReceivingMatch(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawPacket cmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, "match-id-abc", "hunt", 0, true);
    mm->listenForMatchEvents(dummyMac, cmd);

    EXPECT_TRUE(mm->isMatchReady());
}

// clearCurrentMatch resets the matchIsReady flag along with all other duel state.
inline void matchManagerClearMatchResetsMatchIsReadyFlag(MatchManager* mm, Player* player) {
    player->setIsHunter(false);
    static const uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    QuickdrawPacket cmd = makeQuickdrawPacket(QDCommand::SEND_MATCH_ID, "match-id-abc", "hunt", 0, true);
    mm->listenForMatchEvents(dummyMac, cmd);
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
    QuickdrawPacket mismatch = makeQuickdrawPacket(QDCommand::MATCH_ROLE_MISMATCH, matchId, "hunt2", 0, true);
    mm->listenForMatchEvents(dummyMac, mismatch);

    EXPECT_FALSE(mm->getCurrentMatch().has_value());
    EXPECT_FALSE(mm->isMatchReady());
}
