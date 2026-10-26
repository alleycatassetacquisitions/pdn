#include "game/node-network/duel-session.hpp"

#include <cstring>

namespace {
size_t slotIndex(DuelLinkSide side) {
    return side == DuelLinkSide::INITIATOR ? 0 : 1;
}
}  // namespace

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

DuelLinkSide DuelSession::getRemoteSide() const {
    return localSide == DuelLinkSide::INITIATOR ? DuelLinkSide::RESPONDER : DuelLinkSide::INITIATOR;
}

bool DuelSession::recordDrawTime(DuelLinkSide side, uint64_t drawTimeMs) {
    const size_t index = slotIndex(side);
    if (drawTimePresent[index]) {
        return false;
    }
    drawTimePresent[index] = true;
    drawTimes[index] = drawTimeMs;
    return true;
}

bool DuelSession::hasDrawTime(DuelLinkSide side) const {
    return drawTimePresent[slotIndex(side)];
}

uint64_t DuelSession::getDrawTime(DuelLinkSide side) const {
    return drawTimes[slotIndex(side)];
}

bool DuelSession::recordLocalDrawTimeMs(uint64_t drawTimeMs) {
    return recordDrawTime(localSide, drawTimeMs);
}

bool DuelSession::recordRemoteDrawTimeMs(uint64_t drawTimeMs) {
    return recordDrawTime(getRemoteSide(), drawTimeMs);
}

bool DuelSession::hasLocalDrawTime() const {
    return hasDrawTime(localSide);
}

bool DuelSession::hasRemoteDrawTime() const {
    return hasDrawTime(getRemoteSide());
}

bool DuelSession::hasInitiatorDrawTime() const {
    return hasDrawTime(DuelLinkSide::INITIATOR);
}

bool DuelSession::hasResponderDrawTime() const {
    return hasDrawTime(DuelLinkSide::RESPONDER);
}

uint64_t DuelSession::getInitiatorDrawTimeMs() const {
    return getDrawTime(DuelLinkSide::INITIATOR);
}

uint64_t DuelSession::getResponderDrawTimeMs() const {
    return getDrawTime(DuelLinkSide::RESPONDER);
}

void DuelSession::setOpponentNeverPressed() {
    opponentNeverPressed = true;
}

void DuelSession::setGraceExpiredWithoutPress() {
    graceExpiredWithoutPress = true;
}

bool DuelSession::didOpponentNeverPress() const {
    return opponentNeverPressed;
}

bool DuelSession::didGraceExpireWithoutPress() const {
    return graceExpiredWithoutPress;
}

bool DuelSession::isExchangeComplete() const {
    if (hasInitiatorDrawTime() && hasResponderDrawTime()) {
        return true;
    }
    if (graceExpiredWithoutPress) {
        return true;
    }
    return opponentNeverPressed && hasLocalDrawTime();
}

bool DuelSession::tryBuildUploadRecord(QuickdrawMatchRecord& record) const {
    if (!active || !handshakeComplete || !opponentParticipantKnown) {
        return false;
    }
    if (!hasInitiatorDrawTime() || !hasResponderDrawTime()) {
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
    built.playerADrawTimeMs = getInitiatorDrawTimeMs();
    built.playerBDrawTimeMs = getResponderDrawTimeMs();
    built.occurredAtUnixMs = occurredAtUnixMs;
    record = built;
    return true;
}
