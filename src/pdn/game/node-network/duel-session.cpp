#include "game/node-network/duel-session.hpp"

#include <cstring>
#include <initializer_list>

void DuelSession::prime(DuelLinkSide localSide, const UUIDBytes& matchId, const uint8_t* opponentMac,
                        const DuelParticipant& localParticipant, int64_t occurredAtUnixMs) {
    clear();
    active = true;
    this->localSide = localSide;
    this->matchId = matchId;
    memcpy(this->opponentMac, opponentMac, sizeof(this->opponentMac));
    this->localParticipant = localParticipant;
    this->occurredAtUnixMs = occurredAtUnixMs;
}

void DuelSession::clear() {
    *this = DuelSession();
}

bool DuelSession::isActive() const {
    return active;
}

DuelLinkSide DuelSession::getLocalSide() const {
    return localSide;
}

const UUIDBytes& DuelSession::getMatchId() const {
    return matchId;
}

const uint8_t* DuelSession::getOpponentMac() const {
    return opponentMac;
}

bool DuelSession::isFromOpponent(const uint8_t* mac, const UUIDBytes& candidateMatchId) const {
    return active && mac != nullptr && memcmp(mac, opponentMac, sizeof(opponentMac)) == 0 &&
           candidateMatchId == matchId;
}

const DuelParticipant& DuelSession::getLocalParticipant() const {
    return localParticipant;
}

const DuelParticipant& DuelSession::getOpponentParticipant() const {
    return opponentParticipant;
}

bool DuelSession::hasOpponentParticipant() const {
    return opponentParticipantKnown;
}

void DuelSession::setOpponentParticipant(const DuelParticipant& participant) {
    opponentParticipant = participant;
    opponentParticipantKnown = true;
}

bool DuelSession::isHandshakeComplete() const {
    return handshakeComplete;
}

void DuelSession::markHandshakeComplete() {
    handshakeComplete = true;
}

int64_t DuelSession::getOccurredAtUnixMs() const {
    return occurredAtUnixMs;
}

void DuelSession::setOccurredAtUnixMs(int64_t occurredAtUnixMs) {
    this->occurredAtUnixMs = occurredAtUnixMs;
}

DuelLinkSide DuelSession::getOpponentSide() const {
    return localSide == DuelLinkSide::INITIATOR ? DuelLinkSide::RESPONDER : DuelLinkSide::INITIATOR;
}

void DuelSession::recordSyncSample(int32_t offsetMs, uint32_t roundTripMs) {
    if (syncSampleKnown && roundTripMs >= syncRoundTripMs) {
        return;
    }
    syncSampleKnown = true;
    syncOffsetMs = offsetMs;
    syncRoundTripMs = roundTripMs;
}

bool DuelSession::hasSyncSample() const { return syncSampleKnown; }

int32_t DuelSession::getSyncOffsetMs() const { return syncOffsetMs; }

uint32_t DuelSession::getSyncRoundTripMs() const { return syncRoundTripMs; }

bool DuelSession::setSequence(const NodeInputSequence& sequence, uint32_t stepZeroAtMs) {
    if (sequenceKnown) {
        return false;
    }
    this->sequence = sequence;
    this->stepZeroAtMs = stepZeroAtMs;
    sequenceKnown = true;
    return true;
}

bool DuelSession::hasSequence() const { return sequenceKnown; }

const NodeInputSequence& DuelSession::getSequence() const { return sequence; }

uint32_t DuelSession::getStepZeroAtMs() const { return stepZeroAtMs; }

const NodePrompt& DuelSession::getPrompt(uint8_t step, DuelLinkSide side) const {
    static const NodePrompt NO_PROMPT;
    if (step >= NODE_SEQUENCE_LENGTH) {
        return NO_PROMPT;
    }
    const NodeInput& input = sequence.inputs[step];
    return side == DuelLinkSide::INITIATOR ? input.initiator : input.responder;
}

bool DuelSession::presses(uint8_t step, DuelLinkSide side) const {
    return sequenceKnown && getPrompt(step, side).count > 0;
}

bool DuelSession::isLocalPresser(uint8_t step) const { return presses(step, localSide); }

bool DuelSession::isOpponentPresser(uint8_t step) const { return presses(step, getOpponentSide()); }

NodeStepResult& DuelSession::partResult(uint8_t step, DuelLinkSide side) {
    return side == DuelLinkSide::INITIATOR ? initiatorResults[step] : responderResults[step];
}

NodeStepResult DuelSession::getPartResult(uint8_t step, DuelLinkSide side) const {
    return side == DuelLinkSide::INITIATOR ? initiatorResults[step] : responderResults[step];
}

bool DuelSession::recordStepResult(uint8_t step, DuelLinkSide side, bool success) {
    if (!presses(step, side) || getPartResult(step, side) != NodeStepResult::PENDING) {
        return false;
    }
    partResult(step, side) = success ? NodeStepResult::SUCCESS : NodeStepResult::FAILED;
    return true;
}

NodeStepResult DuelSession::getStepResult(uint8_t step) const {
    if (!sequenceKnown || step >= NODE_SEQUENCE_LENGTH) {
        return NodeStepResult::PENDING;
    }
    bool pending = false;
    for (DuelLinkSide side : {DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER}) {
        if (!presses(step, side)) {
            continue;
        }
        const NodeStepResult part = getPartResult(step, side);
        if (part == NodeStepResult::FAILED) {
            return NodeStepResult::FAILED;
        }
        pending = pending || part == NodeStepResult::PENDING;
    }
    return pending ? NodeStepResult::PENDING : NodeStepResult::SUCCESS;
}

bool DuelSession::hasPartResult(uint8_t step, DuelLinkSide side) const {
    return presses(step, side) && getPartResult(step, side) != NodeStepResult::PENDING;
}

void DuelSession::recordOpponentPresses(uint8_t step, uint8_t count) {
    if (step < NODE_SEQUENCE_LENGTH && count > opponentPresses[step]) {
        opponentPresses[step] = count;
    }
}

uint8_t DuelSession::getOpponentPresses(uint8_t step) const {
    return step < NODE_SEQUENCE_LENGTH ? opponentPresses[step] : 0;
}

bool DuelSession::hasStepResult(uint8_t step) const {
    return getStepResult(step) != NodeStepResult::PENDING;
}

bool DuelSession::allStepsResolved() const {
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        if (!hasStepResult(step)) {
            return false;
        }
    }
    return true;
}

void DuelSession::markSequenceDelivered() { sequenceDelivered = true; }

bool DuelSession::isSequenceDelivered() const { return sequenceDelivered; }

void DuelSession::markSequenceUndelivered() { sequenceUndelivered = true; }

bool DuelSession::isSequenceUndelivered() const { return sequenceUndelivered; }

void DuelSession::markOutcomeDelivered() { outcomeDelivered = true; }

bool DuelSession::isOutcomeDelivered() const { return outcomeDelivered; }

void DuelSession::markOutcomeUndelivered() { outcomeUndelivered = true; }

bool DuelSession::isOutcomeUndelivered() const { return outcomeUndelivered; }

uint8_t DuelSession::countSuccesses(DuelLinkSide side) const {
    uint8_t count = 0;
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        if (getStepResult(step) != NodeStepResult::SUCCESS) {
            continue;
        }
        const DuelLinkSide credited = sequence.inputs[step].initiator.count > 0
                                      ? DuelLinkSide::INITIATOR
                                      : DuelLinkSide::RESPONDER;
        if (credited == side) {
            count++;
        }
    }
    return count;
}

void DuelSession::setOutcome(uint8_t initiatorScore, uint8_t responderScore) {
    this->initiatorScore = initiatorScore;
    this->responderScore = responderScore;
    outcomeKnown = true;
}

bool DuelSession::isOutcomeKnown() const { return outcomeKnown; }

uint8_t DuelSession::getInitiatorScore() const { return initiatorScore; }

uint8_t DuelSession::getResponderScore() const { return responderScore; }

uint8_t DuelSession::getBestKnownScore() const {
    if (outcomeKnown) {
        return initiatorScore + responderScore;
    }
    return countSuccesses(DuelLinkSide::INITIATOR) + countSuccesses(DuelLinkSide::RESPONDER);
}

bool DuelSession::isRoundSuccess() const {
    return outcomeKnown && initiatorScore + responderScore >= NODE_SUCCESS_THRESHOLD;
}

bool DuelSession::tryBuildUploadRecord(QuickdrawMatchRecord& record) const {
    if (!active || !handshakeComplete || !opponentParticipantKnown || !outcomeKnown) {
        return false;
    }
    if (localParticipant.playerId.isZero() || opponentParticipant.playerId.isZero()) {
        return false;
    }

    const bool localIsInitiator = localSide == DuelLinkSide::INITIATOR;
    const DuelParticipant& initiator = localIsInitiator ? localParticipant : opponentParticipant;
    const DuelParticipant& responder = localIsInitiator ? opponentParticipant : localParticipant;

    QuickdrawMatchRecord built;
    built.matchId = matchId;
    built.playerAId = initiator.playerId;
    built.playerBId = responder.playerId;
    built.playerATeamId = initiator.teamId;
    built.playerBTeamId = responder.teamId;
    built.winnerPlayerId = initiator.playerId;
    built.playerADrawTimeMs = initiatorScore;
    built.playerBDrawTimeMs = responderScore;
    built.occurredAtUnixMs = occurredAtUnixMs;
    record = built;
    return true;
}
