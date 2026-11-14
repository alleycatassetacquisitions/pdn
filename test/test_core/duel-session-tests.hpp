#pragma once

#include <gtest/gtest.h>

#include <vector>

#include "game/node-network/duel-session.hpp"
#include "game/node-network/node-input-sequence.hpp"
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

inline NodePrompt makeNodePrompt(ButtonIdentifier first) {
    NodePrompt prompt;
    prompt.count = 1;
    prompt.buttons[0] = first;
    return prompt;
}

inline NodePrompt makeNodePrompt(ButtonIdentifier first, ButtonIdentifier second) {
    NodePrompt prompt;
    prompt.count = 2;
    prompt.buttons[0] = first;
    prompt.buttons[1] = second;
    return prompt;
}

/** A step only @p presser presses, with a one-button prompt. */
inline NodeInput makeNodeInput(ButtonIdentifier button, DuelLinkSide presser) {
    NodeInput input;
    (presser == DuelLinkSide::INITIATOR ? input.initiator : input.responder) =
    makeNodePrompt(button);
    return input;
}

/** A step both players press, each with their own prompt. */
inline NodeInput makeBothNodeInput(const NodePrompt& initiator, const NodePrompt& responder) {
    NodeInput input;
    input.initiator = initiator;
    input.responder = responder;
    return input;
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

/** Even steps are @p firstPresser pressing primary; odd steps are @p secondPresser on secondary. */
inline NodeInputSequence makeAlternatingTestSequence(DuelLinkSide firstPresser,
                                                     DuelLinkSide secondPresser) {
    NodeInputSequence sequence;
    for (size_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        sequence.inputs[step] = makeNodeInput(step % 2 == 0 ? ButtonIdentifier::PRIMARY_BUTTON
                                                            : ButtonIdentifier::SECONDARY_BUTTON,
                                              step % 2 == 0 ? firstPresser : secondPresser);
    }
    return sequence;
}

/** Packs one-button steps into SEQUENCE prompt nibbles: initiator low, responder high. */
inline void encodeSingleButtonPrompts(uint8_t* stepPrompts, uint8_t buttonMask,
                                      uint8_t initiatorPresserMask) {
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        const uint8_t nibble = 0x01 | (((buttonMask >> step) & 1u) != 0 ? 0x04 : 0x00);
        stepPrompts[step] = ((initiatorPresserMask >> step) & 1u) != 0
                            ? nibble
                            : static_cast<uint8_t>(nibble << 4);
    }
}

inline void duelSessionStartsInactive() {
    DuelSession session;
    EXPECT_FALSE(session.isActive());
    EXPECT_FALSE(session.isHandshakeComplete());
    EXPECT_FALSE(session.hasSequence());
    EXPECT_FALSE(session.hasSyncSample());
    EXPECT_FALSE(session.isOutcomeKnown());
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
    session.recordSyncSample(10, 5);
    session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER), 1000);
    session.recordStepResult(0, DuelLinkSide::INITIATOR, true);
    session.recordOpponentPresses(1, 1);
    session.markSequenceDelivered();
    session.markSequenceUndelivered();
    session.setOutcome(1, 0);
    session.markOutcomeDelivered();
    session.markOutcomeUndelivered();

    session.clear();

    EXPECT_FALSE(session.isActive());
    EXPECT_FALSE(session.isHandshakeComplete());
    EXPECT_FALSE(session.hasOpponentParticipant());
    EXPECT_FALSE(session.hasSyncSample());
    EXPECT_FALSE(session.hasSequence());
    EXPECT_FALSE(session.hasStepResult(0));
    EXPECT_EQ(session.getOpponentPresses(1), 0);
    EXPECT_FALSE(session.isSequenceDelivered());
    EXPECT_FALSE(session.isSequenceUndelivered());
    EXPECT_FALSE(session.isOutcomeDelivered());
    EXPECT_FALSE(session.isOutcomeUndelivered());
    EXPECT_FALSE(session.isOutcomeKnown());
    EXPECT_EQ(session.getOccurredAtUnixMs(), 0);
}

// ============================================
// Clock sync
// ============================================

inline void duelSessionKeepsSyncSampleWithSmallestRoundTrip() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);

    session.recordSyncSample(100, 40);
    session.recordSyncSample(-7, 12);
    session.recordSyncSample(55, 30);
    session.recordSyncSample(9, 12);

    ASSERT_TRUE(session.hasSyncSample());
    EXPECT_EQ(session.getSyncOffsetMs(), -7);
    EXPECT_EQ(session.getSyncRoundTripMs(), 12u);
}

// ============================================
// Sequence and step results
// ============================================

inline void duelSessionSequenceMapsPresserToSide() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);

    ASSERT_TRUE(session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::RESPONDER, DuelLinkSide::INITIATOR), 4321));

    EXPECT_TRUE(session.hasSequence());
    EXPECT_EQ(session.getStepZeroAtMs(), 4321u);
    EXPECT_FALSE(session.isLocalPresser(0));
    EXPECT_TRUE(session.isOpponentPresser(0));
    EXPECT_TRUE(session.isLocalPresser(1));
    EXPECT_FALSE(session.isOpponentPresser(1));
    EXPECT_FALSE(session.isLocalPresser(NODE_SEQUENCE_LENGTH));
    EXPECT_EQ(session.getPrompt(1, DuelLinkSide::INITIATOR).buttons[0],
              ButtonIdentifier::SECONDARY_BUTTON);
    EXPECT_EQ(session.getPrompt(NODE_SEQUENCE_LENGTH, DuelLinkSide::INITIATOR).count, 0);
}

inline void duelSessionResponderMapsOwnIdToResponderSide() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);

    ASSERT_TRUE(session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::RESPONDER, DuelLinkSide::INITIATOR), 0));

    EXPECT_TRUE(session.isLocalPresser(0));
    EXPECT_FALSE(session.isOpponentPresser(0));
    EXPECT_EQ(session.getOpponentSide(), DuelLinkSide::INITIATOR);
}

inline void duelSessionBothPressStepHasTwoPressers() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    NodeInputSequence sequence =
    makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER);
    sequence.inputs[0] = makeBothNodeInput(
    makeNodePrompt(ButtonIdentifier::PRIMARY_BUTTON),
    makeNodePrompt(ButtonIdentifier::SECONDARY_BUTTON, ButtonIdentifier::PRIMARY_BUTTON));
    ASSERT_TRUE(session.setSequence(sequence, 0));

    EXPECT_TRUE(session.isLocalPresser(0));
    EXPECT_TRUE(session.isOpponentPresser(0));
    EXPECT_EQ(session.getPrompt(0, DuelLinkSide::RESPONDER).count, 2);
}

inline void duelSessionKeepsFirstSequence() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);
    ASSERT_TRUE(session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::RESPONDER, DuelLinkSide::INITIATOR), 100));

    EXPECT_FALSE(session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER), 900));

    EXPECT_EQ(session.getStepZeroAtMs(), 100u);
    EXPECT_TRUE(session.isLocalPresser(0));
}

inline void duelSessionKeepsFirstStepResult() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);

    EXPECT_FALSE(session.recordStepResult(0, DuelLinkSide::INITIATOR, true));

    session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER), 0);
    EXPECT_TRUE(session.recordStepResult(0, DuelLinkSide::INITIATOR, false));
    EXPECT_FALSE(session.recordStepResult(0, DuelLinkSide::INITIATOR, true));
    EXPECT_FALSE(session.recordStepResult(0, DuelLinkSide::RESPONDER, true));
    EXPECT_FALSE(session.recordStepResult(NODE_SEQUENCE_LENGTH, DuelLinkSide::INITIATOR, true));
    EXPECT_EQ(session.getStepResult(0), NodeStepResult::FAILED);
}

inline void duelSessionCountsSuccessesPerPresser() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::RESPONDER, DuelLinkSide::INITIATOR), 0);
    session.recordStepResult(0, DuelLinkSide::RESPONDER, true);
    session.recordStepResult(1, DuelLinkSide::INITIATOR, true);
    session.recordStepResult(2, DuelLinkSide::RESPONDER, false);
    session.recordStepResult(3, DuelLinkSide::INITIATOR, true);

    EXPECT_EQ(session.countSuccesses(DuelLinkSide::INITIATOR), 2);
    EXPECT_EQ(session.countSuccesses(DuelLinkSide::RESPONDER), 1);
    EXPECT_EQ(session.getBestKnownScore(), 3);
    EXPECT_FALSE(session.allStepsResolved());

    session.recordStepResult(4, DuelLinkSide::RESPONDER, false);
    EXPECT_TRUE(session.allStepsResolved());
}

inline void duelSessionBothPressStepNeedsBothParts() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);
    NodeInputSequence sequence =
    makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER);
    const NodePrompt prompt = makeNodePrompt(ButtonIdentifier::PRIMARY_BUTTON);
    sequence.inputs[0] = makeBothNodeInput(prompt, prompt);
    sequence.inputs[1] = makeBothNodeInput(prompt, prompt);
    ASSERT_TRUE(session.setSequence(sequence, 0));

    session.recordStepResult(0, DuelLinkSide::RESPONDER, true);
    EXPECT_EQ(session.getStepResult(0), NodeStepResult::PENDING);
    session.recordStepResult(0, DuelLinkSide::INITIATOR, true);
    EXPECT_EQ(session.getStepResult(0), NodeStepResult::SUCCESS);

    session.recordStepResult(1, DuelLinkSide::INITIATOR, false);
    EXPECT_EQ(session.getStepResult(1), NodeStepResult::FAILED);
    EXPECT_TRUE(session.hasStepResult(1));
    EXPECT_FALSE(session.hasPartResult(1, DuelLinkSide::RESPONDER));

    EXPECT_EQ(session.countSuccesses(DuelLinkSide::INITIATOR), 1);
    EXPECT_EQ(session.countSuccesses(DuelLinkSide::RESPONDER), 0);
}

inline void duelSessionKeepsHighestOpponentPressCount() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::RESPONDER, DuelLinkSide::INITIATOR), 0);

    session.recordOpponentPresses(0, 2);
    session.recordOpponentPresses(0, 1);

    EXPECT_EQ(session.getOpponentPresses(0), 2);
    EXPECT_EQ(session.getOpponentPresses(1), 0);
    EXPECT_EQ(session.getOpponentPresses(NODE_SEQUENCE_LENGTH), 0);
}

// ============================================
// Outcome
// ============================================

inline void duelSessionRoundSucceedsAtThreshold() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);

    session.setOutcome(2, 2);

    EXPECT_TRUE(session.isOutcomeKnown());
    EXPECT_TRUE(session.isRoundSuccess());
    EXPECT_EQ(session.getBestKnownScore(), 4);
}

inline void duelSessionRoundFailsBelowThreshold() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);

    session.setOutcome(1, 2);

    EXPECT_FALSE(session.isRoundSuccess());
    EXPECT_EQ(session.getBestKnownScore(), 3);
}

inline void duelSessionRoundNeedsOutcomeToSucceed() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);
    session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::RESPONDER, DuelLinkSide::RESPONDER), 0);
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        session.recordStepResult(step, DuelLinkSide::RESPONDER, true);
    }

    EXPECT_EQ(session.getBestKnownScore(), static_cast<uint8_t>(NODE_SEQUENCE_LENGTH));
    EXPECT_FALSE(session.isRoundSuccess());
}

inline void duelSessionOutcomeOverridesLocalCount() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);
    session.setSequence(
    makeAlternatingTestSequence(DuelLinkSide::RESPONDER, DuelLinkSide::INITIATOR), 0);
    session.recordStepResult(0, DuelLinkSide::RESPONDER, true);

    session.setOutcome(3, 1);

    EXPECT_EQ(session.getBestKnownScore(), 4);
}

// ============================================
// Upload record
// ============================================

inline void duelSessionRecordUsesInitiatorAsPlayerA() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::RESPONDER);
    session.setOutcome(3, 1);

    QuickdrawMatchRecord record;
    ASSERT_TRUE(session.tryBuildUploadRecord(record));

    EXPECT_EQ(record.matchId, makeTestUuid(DUEL_TEST_MATCH_ID));
    EXPECT_EQ(record.playerAId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(record.playerBId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(record.playerATeamId, makeTestUuid(DUEL_TEST_PEER_TEAM));
    EXPECT_EQ(record.playerBTeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
    EXPECT_EQ(record.playerADrawTimeMs, 3u);
    EXPECT_EQ(record.playerBDrawTimeMs, 1u);
    EXPECT_EQ(record.occurredAtUnixMs, DUEL_TEST_UNIX_MS);
}

inline void duelSessionRecordMapsLocalInitiatorToPlayerA() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setOutcome(2, 3);

    QuickdrawMatchRecord record;
    ASSERT_TRUE(session.tryBuildUploadRecord(record));

    EXPECT_EQ(record.playerAId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(record.playerBId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(record.playerADrawTimeMs, 2u);
    EXPECT_EQ(record.playerBDrawTimeMs, 3u);
}

inline void duelSessionRecordWinnerIsInitiatorRegardlessOfScores() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setOutcome(0, 5);

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
    session.setOutcome(2, 2);

    QuickdrawMatchRecord record;
    ASSERT_TRUE(session.tryBuildUploadRecord(record));

    EXPECT_EQ(record.playerATeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
    EXPECT_TRUE(record.playerBTeamId.isZero());
}

inline void duelSessionRecordFailsWithoutOutcome() {
    DuelSession session;
    primeCompleteDuelTestSession(session, DuelLinkSide::INITIATOR);

    QuickdrawMatchRecord record;
    record.playerADrawTimeMs = 77;
    EXPECT_FALSE(session.tryBuildUploadRecord(record));
    EXPECT_EQ(record.playerADrawTimeMs, 77u);
}

inline void duelSessionRecordFailsBeforeHandshakeComplete() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setOpponentParticipant(makeDuelParticipant(DUEL_TEST_PEER_ID, DUEL_TEST_PEER_TEAM));
    session.setOutcome(2, 2);

    QuickdrawMatchRecord record;
    EXPECT_FALSE(session.tryBuildUploadRecord(record));
}

inline void duelSessionRecordFailsWhenOpponentIdIsNil() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::INITIATOR);
    session.setOpponentParticipant(DuelParticipant());
    session.markHandshakeComplete();
    session.setOutcome(2, 2);

    QuickdrawMatchRecord record;
    EXPECT_FALSE(session.tryBuildUploadRecord(record));
}

inline void duelSessionTimestampCanBeCorrected() {
    DuelSession session;
    primeDuelTestSession(session, DuelLinkSide::INITIATOR);

    session.setOccurredAtUnixMs(1800000000000);

    EXPECT_EQ(session.getOccurredAtUnixMs(), 1800000000000);
}

// ============================================
// Sequence generation
// ============================================

inline void expectPrompt(const NodePrompt& prompt, std::vector<ButtonIdentifier> buttons) {
    ASSERT_EQ(prompt.count, buttons.size());
    for (size_t press = 0; press < buttons.size(); press++) {
        EXPECT_EQ(prompt.buttons[press], buttons[press]);
    }
}

inline void nodeInputSequenceUsesRandomForKindButtonsAndSide() {
    const std::vector<uint32_t> rolls = {
    0, 0, 0,           // single, primary, initiator
    1, 1, 1,           // single, secondary, responder
    2, 1, 0, 0,        // double, secondary then primary, initiator
    10, 0, 1, 1,       // double (10 % 6 == 4), primary then secondary, responder
    5, 0, 1, 1, 0, 0,  // both: initiator one secondary, responder two primaries
    };
    size_t next = 0;
    const NodeRandomSource random = [&rolls, &next]() { return rolls[next++]; };
    const ButtonIdentifier P = ButtonIdentifier::PRIMARY_BUTTON;
    const ButtonIdentifier S = ButtonIdentifier::SECONDARY_BUTTON;

    const NodeInputSequence sequence = generateNodeInputSequence(random);

    EXPECT_EQ(next, rolls.size());
    expectPrompt(sequence.inputs[0].initiator, {P});
    expectPrompt(sequence.inputs[0].responder, {});
    expectPrompt(sequence.inputs[1].initiator, {});
    expectPrompt(sequence.inputs[1].responder, {S});
    expectPrompt(sequence.inputs[2].initiator, {S, P});
    expectPrompt(sequence.inputs[2].responder, {});
    expectPrompt(sequence.inputs[3].initiator, {});
    expectPrompt(sequence.inputs[3].responder, {P, S});
    expectPrompt(sequence.inputs[4].initiator, {S});
    expectPrompt(sequence.inputs[4].responder, {P, P});
    EXPECT_TRUE(nodeStepIsBothPress(sequence.inputs[4]));
    EXPECT_FALSE(nodeStepIsBothPress(sequence.inputs[3]));
}

inline void nodeStepTimingDependsOnPromptLength() {
    const NodePrompt single = makeNodePrompt(ButtonIdentifier::PRIMARY_BUTTON);
    const NodePrompt twoButtons =
    makeNodePrompt(ButtonIdentifier::PRIMARY_BUTTON, ButtonIdentifier::SECONDARY_BUTTON);
    NodeInputSequence sequence;
    sequence.inputs[0] = makeNodeInput(ButtonIdentifier::PRIMARY_BUTTON, DuelLinkSide::INITIATOR);
    sequence.inputs[1].responder = twoButtons;
    sequence.inputs[2] = makeBothNodeInput(single, twoButtons);
    sequence.inputs[3] = makeBothNodeInput(single, single);
    sequence.inputs[4] = makeNodeInput(ButtonIdentifier::PRIMARY_BUTTON, DuelLinkSide::RESPONDER);
    const unsigned long singlePeriod = NODE_INPUT_WINDOW_MS + NODE_STEP_GAP_MS;
    const unsigned long doublePeriod = NODE_DOUBLE_INPUT_WINDOW_MS + NODE_DOUBLE_STEP_GAP_MS;
    const unsigned long bothPeriod = NODE_BOTH_INPUT_WINDOW_MS + NODE_BOTH_STEP_GAP_MS;

    EXPECT_EQ(nodeStepWindowMs(sequence.inputs[0]), NODE_INPUT_WINDOW_MS);
    EXPECT_EQ(nodeStepWindowMs(sequence.inputs[1]), NODE_DOUBLE_INPUT_WINDOW_MS);
    EXPECT_EQ(nodeStepWindowMs(sequence.inputs[2]), NODE_BOTH_INPUT_WINDOW_MS);
    EXPECT_EQ(nodeStepWindowMs(sequence.inputs[3]), NODE_BOTH_INPUT_WINDOW_MS);
    EXPECT_EQ(nodeStepOffsetMs(sequence, 0), 0u);
    EXPECT_EQ(nodeStepOffsetMs(sequence, 1), singlePeriod);
    EXPECT_EQ(nodeStepOffsetMs(sequence, 2), singlePeriod + doublePeriod);
    EXPECT_EQ(nodeStepOffsetMs(sequence, 4), singlePeriod + doublePeriod + 2 * bothPeriod);
}
