#pragma once

#include <cstdint>

#include "alleycat-server/quickdraw-match-codec.hpp"
#include "game/node-network/node-input-sequence.hpp"
#include "utils/uuid-bytes.hpp"

/** Which end of the cable this device is: OUTPUT jack initiates, INPUT jack responds. */
enum class DuelLinkSide {
    INITIATOR,
    RESPONDER,
};

/** One duelist as the server knows them. A nil team id means no team. */
struct DuelParticipant {
    UUIDBytes playerId;
    UUIDBytes teamId;
};

enum class NodeStepResult : uint8_t {
    PENDING,
    SUCCESS,
    FAILED,
};

/** State of a single round, from handshake to upload record. Holds no timing policy. */
class DuelSession {
public:
    /** Starts a session; clears any earlier state. */
    void prime(DuelLinkSide localSide, const UUIDBytes& matchId, const uint8_t* opponentMac,
               const DuelParticipant& localParticipant, int64_t occurredAtUnixMs);

    /** Returns to the inactive state. */
    void clear();

    bool isActive() const;
    DuelLinkSide getLocalSide() const;
    DuelLinkSide getOpponentSide() const;
    const UUIDBytes& getMatchId() const;
    const uint8_t* getOpponentMac() const;

    /** @return true when @p mac is the opponent's and @p matchId is this session's match. */
    bool isFromOpponent(const uint8_t* mac, const UUIDBytes& matchId) const;

    const DuelParticipant& getLocalParticipant() const;
    const DuelParticipant& getOpponentParticipant() const;
    bool hasOpponentParticipant() const;
    void setOpponentParticipant(const DuelParticipant& participant);

    bool isHandshakeComplete() const;
    void markHandshakeComplete();

    int64_t getOccurredAtUnixMs() const;
    void setOccurredAtUnixMs(int64_t occurredAtUnixMs);

    /** Keeps the clock-sync sample with the smallest round trip. */
    void recordSyncSample(int32_t offsetMs, uint32_t roundTripMs);

    /** @return true once any sync sample was recorded. */
    bool hasSyncSample() const;

    /** Responder uptime minus initiator uptime, from the best sample. */
    int32_t getSyncOffsetMs() const;

    /** Round trip of the best sample. */
    uint32_t getSyncRoundTripMs() const;

    /**
     * Stores the round and when step 0 starts on this device's uptime. Later calls are ignored.
     *
     * @return false when a sequence is already set.
     */
    bool setSequence(const NodeInputSequence& sequence, uint32_t stepZeroAtMs);

    /** @return true once setSequence has been called. */
    bool hasSequence() const;

    /** @return the round's steps. */
    const NodeInputSequence& getSequence() const;

    /** @return step 0's start on this device's uptime. */
    uint32_t getStepZeroAtMs() const;

    /** @return what @p side must press in @p step; an empty prompt when out of range. */
    const NodePrompt& getPrompt(uint8_t step, DuelLinkSide side) const;

    /** @return true when @p side presses in @p step; false without a sequence. */
    bool presses(uint8_t step, DuelLinkSide side) const;

    /** @return true when the local player presses @p step. */
    bool isLocalPresser(uint8_t step) const;

    /** @return true when the opponent presses @p step. */
    bool isOpponentPresser(uint8_t step) const;

    /**
     * Records @p side's part of @p step.
     *
     * @return false, keeping the first result, without a sequence, out of range, when @p side
     *         does not press the step, or when its part is already resolved.
     */
    bool recordStepResult(uint8_t step, DuelLinkSide side, bool success);

    /** @return PENDING until every presser's part is in, FAILED as soon as any part failed. */
    NodeStepResult getStepResult(uint8_t step) const;

    /** @return true when @p side presses @p step and its part has a result. */
    bool hasPartResult(uint8_t step, DuelLinkSide side) const;

    /** Notes how many of its buttons the opponent has pressed in @p step; never decreases. */
    void recordOpponentPresses(uint8_t step, uint8_t count);

    /** @return buttons the opponent has pressed in @p step, as far as this device knows. */
    uint8_t getOpponentPresses(uint8_t step) const;

    /** @return true when @p step has a SUCCESS or FAILED result. */
    bool hasStepResult(uint8_t step) const;

    /** @return true when every step has a result. */
    bool allStepsResolved() const;

    /**
     * Successful steps credited to @p side: one-player steps to their presser, both-player steps
     * to the initiator, so the two counts add up to the round's score.
     */
    uint8_t countSuccesses(DuelLinkSide side) const;

    /** Initiator: the responder acknowledged SEQUENCE. */
    void markSequenceDelivered();

    /** @return true once the responder acknowledged SEQUENCE. */
    bool isSequenceDelivered() const;

    /** Initiator: SEQUENCE ran out of resends. */
    void markSequenceUndelivered();

    /** @return true when SEQUENCE ran out of resends. */
    bool isSequenceUndelivered() const;

    /** Initiator: the responder acknowledged OUTCOME. */
    void markOutcomeDelivered();

    /** @return true once the responder acknowledged OUTCOME. */
    bool isOutcomeDelivered() const;

    /** Initiator: OUTCOME ran out of resends. */
    void markOutcomeUndelivered();

    /** @return true when OUTCOME ran out of resends. */
    bool isOutcomeUndelivered() const;

    /** Stores the coordinator's final per-side success counts. */
    void setOutcome(uint8_t initiatorScore, uint8_t responderScore);

    /** @return true once setOutcome has been called. */
    bool isOutcomeKnown() const;

    /** @return the initiator's successes from the outcome. */
    uint8_t getInitiatorScore() const;

    /** @return the responder's successes from the outcome. */
    uint8_t getResponderScore() const;

    /** The outcome total when known, otherwise the successes recorded on this device. */
    uint8_t getBestKnownScore() const;

    /** @return true when the outcome is known and reaches NODE_SUCCESS_THRESHOLD. */
    bool isRoundSuccess() const;

    /**
     * Builds the upload record with the initiator as player A. The draw-time fields carry each
     * player's successful inputs.
     *
     * @return false, leaving @p record unchanged, unless the handshake is complete, the outcome
     *         is known, and both player ids are non-nil.
     */
    bool tryBuildUploadRecord(QuickdrawMatchRecord& record) const;

private:
    NodeStepResult& partResult(uint8_t step, DuelLinkSide side);
    NodeStepResult getPartResult(uint8_t step, DuelLinkSide side) const;

    bool active = false;
    bool handshakeComplete = false;
    bool opponentParticipantKnown = false;
    bool syncSampleKnown = false;
    int32_t syncOffsetMs = 0;
    uint32_t syncRoundTripMs = 0;
    bool sequenceKnown = false;
    NodeInputSequence sequence;
    uint32_t stepZeroAtMs = 0;
    bool sequenceDelivered = false;
    bool sequenceUndelivered = false;
    bool outcomeDelivered = false;
    bool outcomeUndelivered = false;
    NodeStepResult initiatorResults[NODE_SEQUENCE_LENGTH] = {};
    NodeStepResult responderResults[NODE_SEQUENCE_LENGTH] = {};
    uint8_t opponentPresses[NODE_SEQUENCE_LENGTH] = {};
    bool outcomeKnown = false;
    uint8_t initiatorScore = 0;
    uint8_t responderScore = 0;
    DuelLinkSide localSide = DuelLinkSide::INITIATOR;
    UUIDBytes matchId;
    uint8_t opponentMac[6] = {};
    DuelParticipant localParticipant;
    DuelParticipant opponentParticipant;
    int64_t occurredAtUnixMs = 0;
};
