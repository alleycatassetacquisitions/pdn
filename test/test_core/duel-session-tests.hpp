#pragma once

#include <gtest/gtest.h>

#include "game/node-network/duel-session.hpp"
#include "uuid-bytes-tests.hpp"

inline constexpr const char* DUEL_TEST_LOCAL_ID = "11111111-1111-4111-8111-111111111111";
inline constexpr const char* DUEL_TEST_LOCAL_TEAM = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
inline constexpr const char* DUEL_TEST_PEER_ID = "22222222-2222-4222-8222-222222222222";
inline constexpr const char* DUEL_TEST_PEER_TEAM = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
inline constexpr const char* DUEL_TEST_MATCH_ID = "33333333-3333-4333-8333-333333333333";
inline constexpr uint8_t DUEL_TEST_PEER_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
inline constexpr uint8_t DUEL_TEST_OTHER_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x09};
inline constexpr int64_t DUEL_TEST_UNIX_MS = 1700000000000;

inline DuelParticipant makeDuelParticipant(const char* playerId, const char* teamId) {
    DuelParticipant participant;
    participant.playerId = makeTestUuid(playerId);
    if (teamId != nullptr) {
        participant.teamId = makeTestUuid(teamId);
    }
    return participant;
}

/** Primes a session as the given side with the local participant only. */
inline void primeDuelTestSession(DuelSession& session, DuelLinkSide localSide) {
    session.prime(localSide, makeTestUuid(DUEL_TEST_MATCH_ID), DUEL_TEST_PEER_MAC,
                  makeDuelParticipant(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM), DUEL_TEST_UNIX_MS);
}

/** Primes, learns the opponent, and completes the handshake. */
inline void primeCompleteDuelTestSession(DuelSession& session, DuelLinkSide localSide) {
    primeDuelTestSession(session, localSide);
    session.setOpponentParticipant(makeDuelParticipant(DUEL_TEST_PEER_ID, DUEL_TEST_PEER_TEAM));
    session.markHandshakeComplete();
}

inline void duelSessionStartsInactive() {
    DuelSession session;
    EXPECT_FALSE(session.isActive());
    EXPECT_FALSE(session.isHandshakeComplete());
    EXPECT_FALSE(session.isExchangeComplete());
}

inline void duelSessionPrimeStoresHandshakeFields() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::RESPONDER);

    EXPECT_TRUE(session.isActive());
    EXPECT_EQ(session.getLocalSide(), DuelLinkSide::RESPONDER);
    EXPECT_EQ(session.getMatchId(), makeTestUuid(DUEL_TEST_MATCH_ID));
    EXPECT_EQ(memcmp(session.getOpponentMac(), DUEL_TEST_PEER_MAC, 6), 0);
    EXPECT_EQ(session.getLocalParticipant().playerId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(session.getLocalParticipant().teamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
    EXPECT_EQ(session.getOccurredAtUnixMs(), DUEL_TEST_UNIX_MS);
    EXPECT_FALSE(session.isHandshakeComplete());
    EXPECT_FALSE(session.hasOpponentParticipant());
}

inline void duelSessionStoresOpponentParticipant() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setOpponentParticipant(makeDuelParticipant(DUEL_TEST_PEER_ID, DUEL_TEST_PEER_TEAM));

    EXPECT_TRUE(session.hasOpponentParticipant());
    EXPECT_EQ(session.getOpponentParticipant().playerId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(session.getOpponentParticipant().teamId, makeTestUuid(DUEL_TEST_PEER_TEAM));
}

inline void duelSessionMatchesOnlyActiveOpponent() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::INITIATOR);

    EXPECT_TRUE(session.isFromOpponent(DUEL_TEST_PEER_MAC, makeTestUuid(DUEL_TEST_MATCH_ID)));
    EXPECT_FALSE(session.isFromOpponent(DUEL_TEST_OTHER_MAC, makeTestUuid(DUEL_TEST_MATCH_ID)));
    EXPECT_FALSE(session.isFromOpponent(DUEL_TEST_PEER_MAC, makeTestUuid(DUEL_TEST_PEER_ID)));
}

inline void duelSessionClearResetsEverything() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.recordLocalDrawTimeMs(200);
    session.recordRemoteDrawTimeMs(220);
    session.setOpponentNeverPressed();
    session.setGraceExpiredWithoutPress();

    session.clear();

    EXPECT_FALSE(session.isActive());
    EXPECT_FALSE(session.isHandshakeComplete());
    EXPECT_FALSE(session.hasOpponentParticipant());
    EXPECT_FALSE(session.hasInitiatorDrawTime());
    EXPECT_FALSE(session.hasResponderDrawTime());
    EXPECT_FALSE(session.didOpponentNeverPress());
    EXPECT_FALSE(session.didGraceExpireWithoutPress());
    EXPECT_EQ(session.getOccurredAtUnixMs(), 0);
}

inline void duelSessionLocalInitiatorWritesInitiatorSlot() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);

    EXPECT_TRUE(session.recordLocalDrawTimeMs(200));

    EXPECT_TRUE(session.hasInitiatorDrawTime());
    EXPECT_EQ(session.getInitiatorDrawTimeMs(), 200u);
    EXPECT_FALSE(session.hasResponderDrawTime());
    EXPECT_TRUE(session.hasLocalDrawTime());
    EXPECT_FALSE(session.hasRemoteDrawTime());
}

inline void duelSessionLocalResponderWritesResponderSlot() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);

    EXPECT_TRUE(session.recordLocalDrawTimeMs(310));

    EXPECT_TRUE(session.hasResponderDrawTime());
    EXPECT_EQ(session.getResponderDrawTimeMs(), 310u);
    EXPECT_FALSE(session.hasInitiatorDrawTime());
}

inline void duelSessionRemoteDrawFillsOppositeSlot() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);

    EXPECT_TRUE(session.recordRemoteDrawTimeMs(180));

    EXPECT_TRUE(session.hasInitiatorDrawTime());
    EXPECT_EQ(session.getInitiatorDrawTimeMs(), 180u);
    EXPECT_FALSE(session.hasResponderDrawTime());
    EXPECT_TRUE(session.hasRemoteDrawTime());
}

inline void duelSessionIgnoresDuplicateDraws() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);

    EXPECT_TRUE(session.recordRemoteDrawTimeMs(300));
    EXPECT_FALSE(session.recordRemoteDrawTimeMs(999));
    EXPECT_TRUE(session.recordLocalDrawTimeMs(200));
    EXPECT_FALSE(session.recordLocalDrawTimeMs(1));

    EXPECT_EQ(session.getResponderDrawTimeMs(), 300u);
    EXPECT_EQ(session.getInitiatorDrawTimeMs(), 200u);
}

inline void duelSessionExchangeCompleteWhenBothTimesPresent() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.recordLocalDrawTimeMs(200);
    session.recordRemoteDrawTimeMs(240);

    EXPECT_TRUE(session.isExchangeComplete());
}

inline void duelSessionExchangeIncompleteWithOnlyLocalTime() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.recordLocalDrawTimeMs(200);

    EXPECT_FALSE(session.isExchangeComplete());
}

inline void duelSessionExchangeCompleteWhenOpponentNeverPressedAfterLocalPress() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.recordLocalDrawTimeMs(200);
    session.setOpponentNeverPressed();

    EXPECT_TRUE(session.isExchangeComplete());
}

inline void duelSessionOpponentNeverPressedWaitsForLocalPress() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setOpponentNeverPressed();

    EXPECT_FALSE(session.isExchangeComplete());
}

inline void duelSessionExchangeCompleteWhenGraceExpiresWithoutPress() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);
    session.setGraceExpiredWithoutPress();

    EXPECT_TRUE(session.isExchangeComplete());
}

inline void duelSessionRecordUsesInitiatorAsPlayerA() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);
    session.recordRemoteDrawTimeMs(200);
    session.recordLocalDrawTimeMs(240);

    QuickdrawMatchRecord record;
    ASSERT_TRUE(session.tryBuildUploadRecord(record));

    EXPECT_EQ(record.matchId, makeTestUuid(DUEL_TEST_MATCH_ID));
    EXPECT_EQ(record.playerAId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(record.playerBId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(record.playerATeamId, makeTestUuid(DUEL_TEST_PEER_TEAM));
    EXPECT_EQ(record.playerBTeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
    EXPECT_EQ(record.playerADrawTimeMs, 200u);
    EXPECT_EQ(record.playerBDrawTimeMs, 240u);
    EXPECT_EQ(record.occurredAtUnixMs, DUEL_TEST_UNIX_MS);
}

inline void duelSessionRecordMapsLocalInitiatorToPlayerA() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.recordLocalDrawTimeMs(200);
    session.recordRemoteDrawTimeMs(240);

    QuickdrawMatchRecord record;
    ASSERT_TRUE(session.tryBuildUploadRecord(record));

    EXPECT_EQ(record.playerAId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(record.playerBId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(record.playerADrawTimeMs, 200u);
    EXPECT_EQ(record.playerBDrawTimeMs, 240u);
}

inline void duelSessionRecordWinnerIsInitiatorRegardlessOfTimes() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.recordLocalDrawTimeMs(500);
    session.recordRemoteDrawTimeMs(100);

    QuickdrawMatchRecord record;
    ASSERT_TRUE(session.tryBuildUploadRecord(record));

    EXPECT_EQ(record.winnerPlayerId, record.playerAId);
}

inline void duelSessionRecordKeepsNilTeamNil() {
    DuelSession session;
    session.prime(DuelLinkSide::INITIATOR, makeTestUuid(DUEL_TEST_MATCH_ID), DUEL_TEST_PEER_MAC,
                  makeDuelParticipant(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM), DUEL_TEST_UNIX_MS);
    session.setOpponentParticipant(makeDuelParticipant(DUEL_TEST_PEER_ID, nullptr));
    session.markHandshakeComplete();
    session.recordLocalDrawTimeMs(200);
    session.recordRemoteDrawTimeMs(220);

    QuickdrawMatchRecord record;
    ASSERT_TRUE(session.tryBuildUploadRecord(record));

    EXPECT_EQ(record.playerATeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
    EXPECT_TRUE(record.playerBTeamId.isZero());
}

inline void duelSessionRecordFailsWhenResponderTimeMissing() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.recordLocalDrawTimeMs(200);

    QuickdrawMatchRecord record;
    record.playerADrawTimeMs = 77;
    EXPECT_FALSE(session.tryBuildUploadRecord(record));
    EXPECT_EQ(record.playerADrawTimeMs, 77u);
}

inline void duelSessionRecordFailsBeforeHandshakeComplete() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setOpponentParticipant(makeDuelParticipant(DUEL_TEST_PEER_ID, DUEL_TEST_PEER_TEAM));
    session.recordLocalDrawTimeMs(200);
    session.recordRemoteDrawTimeMs(220);

    QuickdrawMatchRecord record;
    EXPECT_FALSE(session.tryBuildUploadRecord(record));
}

inline void duelSessionRecordFailsWhenOpponentIdIsNil() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setOpponentParticipant(DuelParticipant());
    session.markHandshakeComplete();
    session.recordLocalDrawTimeMs(200);
    session.recordRemoteDrawTimeMs(220);

    QuickdrawMatchRecord record;
    EXPECT_FALSE(session.tryBuildUploadRecord(record));
}

inline void duelSessionTimestampCanBeCorrected() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::INITIATOR);

    session.setOccurredAtUnixMs(1800000000000);

    EXPECT_EQ(session.getOccurredAtUnixMs(), 1800000000000);
}
