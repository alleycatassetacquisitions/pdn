#include "game/node-network/match-manager-v2.hpp"

#include <cstring>
#include <initializer_list>

#include "id-generator.hpp"
#define TAG "MMV2"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"

namespace {
constexpr uint8_t PROMPT_COUNT_MASK = 0x03;
constexpr uint8_t PROMPT_FIRST_SECONDARY = 0x04;
constexpr uint8_t PROMPT_SECOND_SECONDARY = 0x08;
constexpr uint8_t PROMPT_NIBBLE_BITS = 4;

uint8_t encodePrompt(const NodePrompt& prompt) {
    uint8_t nibble = prompt.count & PROMPT_COUNT_MASK;
    if (prompt.buttons[0] == ButtonIdentifier::SECONDARY_BUTTON) {
        nibble |= PROMPT_FIRST_SECONDARY;
    }
    if (prompt.buttons[1] == ButtonIdentifier::SECONDARY_BUTTON) {
        nibble |= PROMPT_SECOND_SECONDARY;
    }
    return nibble;
}

/** @return false when the nibble asks for more buttons than a step allows. */
bool decodePrompt(uint8_t nibble, NodePrompt& prompt) {
    prompt.count = nibble & PROMPT_COUNT_MASK;
    prompt.buttons[0] = (nibble & PROMPT_FIRST_SECONDARY) != 0 ? ButtonIdentifier::SECONDARY_BUTTON
                                                               : ButtonIdentifier::PRIMARY_BUTTON;
    prompt.buttons[1] = (nibble & PROMPT_SECOND_SECONDARY) != 0 ? ButtonIdentifier::SECONDARY_BUTTON
                                                                : ButtonIdentifier::PRIMARY_BUTTON;
    return prompt.count <= NODE_MAX_STEP_INPUTS;
}
}  // namespace

MatchManagerV2::MatchManagerV2(PlayerProvider* playerProvider, StorageInterface* storageInterface,
                               NodeNetworkWirelessManager* nodeNetworkWirelessManager,
                               RemoteDeviceCoordinator* remoteDeviceCoordinator,
                               PlatformClock* platformClock)
    : playerProvider(playerProvider)
    , nodeNetworkWirelessManager(nodeNetworkWirelessManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , platformClock(platformClock)
    , matchStorage(new MatchStorage(storageInterface)) {
    this->nodeNetworkWirelessManager->setPacketReceivedCallback(
        [this](const uint8_t* senderMac, const NodeNetworkPacket& packet) {
            onPacket(senderMac, packet);
        });
    this->nodeNetworkWirelessManager->setDeliveryCallback(
    [this](const uint8_t* destinationMac, const NodeNetworkPacket& packet, bool delivered) {
        onDelivery(destinationMac, packet, delivered);
    });
}

MatchManagerV2::~MatchManagerV2() {
    nodeNetworkWirelessManager->clearCallbacks();
    nodeNetworkWirelessManager = nullptr;
    playerProvider = nullptr;
    remoteDeviceCoordinator = nullptr;
    platformClock = nullptr;
    MatchStorage* ownedStorage = matchStorage;
    matchStorage = nullptr;
    delete ownedStorage;
}

void MatchManagerV2::setLocalLinkSide(DuelLinkSide side) {
    localLinkSide = side;
    localLinkSideAssigned = true;
}

void MatchManagerV2::clearLocalLinkSide() {
    localLinkSideAssigned = false;
}

bool MatchManagerV2::buildLocalParticipant(DuelParticipant& participant) const {
    const Player* player = playerProvider->getPlayer();
    if (!player->hasRegistrationProfile() || player->getServerPlayerId().isZero()) {
        return false;
    }
    participant.playerId = player->getServerPlayerId();
    participant.teamId = player->getRegistrationTeam().id;
    return true;
}

void MatchManagerV2::trackPeer(const uint8_t* peerMac) {
    if (!hasTrackedPeer || memcmp(trackedPeerMac, peerMac, sizeof(trackedPeerMac)) != 0) {
        failedAttempts = 0;
    }
    hasTrackedPeer = true;
    memcpy(trackedPeerMac, peerMac, sizeof(trackedPeerMac));
}

void MatchManagerV2::sendReply(const uint8_t* destinationMac, NodeNetworkCommand command,
                               const UUIDBytes& matchId, int64_t unixMs) const {
    NodeNetworkPacket packet;
    packet.command = static_cast<uint8_t>(command);
    packet.senderIsInitiator = localLinkSide == DuelLinkSide::INITIATOR ? 1 : 0;
    nodeNetworkPacketSetUuid(packet.matchId, matchId);
    packet.occurredAtUnixMs = unixMs;
    DuelParticipant local;
    if (buildLocalParticipant(local)) {
        nodeNetworkPacketSetUuid(packet.senderPlayerId, local.playerId);
        nodeNetworkPacketSetUuid(packet.senderTeamId, local.teamId);
    }
    if (nodeNetworkWirelessManager->sendPacket(destinationMac, packet) < 0) {
        LOG_E(TAG, "send cmd %u to %s failed", packet.command, MacToString(destinationMac));
    }
}

void MatchManagerV2::sendOffer() {
    sendReply(session.getOpponentMac(), NodeNetworkCommand::OFFER, session.getMatchId(),
              session.getOccurredAtUnixMs());
}

NodeNetworkPacket MatchManagerV2::makeSessionPacket(NodeNetworkCommand command) const {
    NodeNetworkPacket packet;
    packet.command = static_cast<uint8_t>(command);
    packet.senderIsInitiator = session.getLocalSide() == DuelLinkSide::INITIATOR ? 1 : 0;
    nodeNetworkPacketSetUuid(packet.matchId, session.getMatchId());
    nodeNetworkPacketSetUuid(packet.senderPlayerId, session.getLocalParticipant().playerId);
    nodeNetworkPacketSetUuid(packet.senderTeamId, session.getLocalParticipant().teamId);
    return packet;
}

void MatchManagerV2::sendToOpponent(const NodeNetworkPacket& packet) {
    if (nodeNetworkWirelessManager->sendPacket(session.getOpponentMac(), packet) < 0) {
        LOG_E(TAG, "send cmd %u to %s failed", packet.command,
              MacToString(session.getOpponentMac()));
    }
}

void MatchManagerV2::sendReliableToOpponent(const NodeNetworkPacket& packet) {
    if (nodeNetworkWirelessManager->sendReliable(session.getOpponentMac(), packet) < 0) {
        LOG_E(TAG, "reliable send cmd %u to %s failed", packet.command,
              MacToString(session.getOpponentMac()));
    }
}

uint32_t MatchManagerV2::nowMs() const {
    return static_cast<uint32_t>(platformClock->milliseconds());
}

bool MatchManagerV2::startMatch(const uint8_t* opponentMac) {
    if (!localLinkSideAssigned || localLinkSide != DuelLinkSide::INITIATOR || session.isActive()) {
        return false;
    }

    DuelParticipant local;
    if (!buildLocalParticipant(local)) {
        LOG_W(TAG, "startMatch refused: player is not registered");
        return false;
    }

    const int64_t unixMs = platformClock->unixMilliseconds();
    if (unixMs <= 0) {
        LOG_W(TAG, "startMatch refused: no valid unix time");
        return false;
    }

    UUIDBytes matchId;
    if (!UUIDBytes::parse(IdGenerator::getInstance().generateId(), matchId)) {
        return false;
    }

    trackPeer(opponentMac);
    timeCorrectionApplied = false;
    aborted = false;
    session.prime(DuelLinkSide::INITIATOR, matchId, opponentMac, local, unixMs);
    LOG_I(TAG, "OFFER -> %s", MacToString(opponentMac));
    sendOffer();
    return true;
}

void MatchManagerV2::onPacket(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    switch (static_cast<NodeNetworkCommand>(packet.command)) {
        case NodeNetworkCommand::OFFER:
            onOffer(senderMac, packet);
            break;
        case NodeNetworkCommand::ACCEPT:
            onAccept(senderMac, packet);
            break;
        case NodeNetworkCommand::ROLE_MISMATCH:
            if (session.isFromOpponent(senderMac, nodeNetworkPacketGetUuid(packet.matchId))) {
                LOG_W(TAG, "ROLE_MISMATCH from %s: dropping session", MacToString(senderMac));
                clearCurrentMatch();
            }
            break;
        case NodeNetworkCommand::TIME_CORRECTION:
            onTimeCorrection(senderMac, packet);
            break;
        case NodeNetworkCommand::SYNC: onSync(senderMac, packet); break;
        case NodeNetworkCommand::SYNC_REPLY: onSyncReply(senderMac, packet); break;
        case NodeNetworkCommand::SEQUENCE: onSequence(senderMac, packet); break;
        case NodeNetworkCommand::INPUT_RESULT: onInputResult(senderMac, packet); break;
        case NodeNetworkCommand::INPUT_PRESS: onInputPress(senderMac, packet); break;
        case NodeNetworkCommand::OUTCOME: onOutcome(senderMac, packet); break;
        case NodeNetworkCommand::ABORT: onAbort(senderMac, packet); break;
        case NodeNetworkCommand::ACK: break;
    }
}

void MatchManagerV2::onDelivery(const uint8_t* destinationMac, const NodeNetworkPacket& packet,
                                bool delivered) {
    if (!session.isFromOpponent(destinationMac, nodeNetworkPacketGetUuid(packet.matchId))) {
        return;
    }
    if (packet.command == static_cast<uint8_t>(NodeNetworkCommand::SEQUENCE)) {
        if (delivered) {
            session.markSequenceDelivered();
        } else {
            LOG_W(TAG, "SEQUENCE to %s undelivered", MacToString(destinationMac));
            session.markSequenceUndelivered();
        }
    } else if (packet.command == static_cast<uint8_t>(NodeNetworkCommand::OUTCOME)) {
        if (delivered) {
            session.markOutcomeDelivered();
        } else {
            LOG_W(TAG, "OUTCOME to %s undelivered", MacToString(destinationMac));
            session.markOutcomeUndelivered();
        }
    }
}

void MatchManagerV2::onOffer(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    // Our serial handshake may finish after the peer's; the initiator re-offers on timeout.
    if (!localLinkSideAssigned) {
        LOG_W(TAG, "OFFER from %s dropped: link side not assigned", MacToString(senderMac));
        return;
    }

    if (!remoteDeviceCoordinator->isDirectPeer(senderMac)) {
        LOG_W(TAG, "OFFER from %s dropped: not a direct peer", MacToString(senderMac));
        return;
    }

    const UUIDBytes offerMatchId = nodeNetworkPacketGetUuid(packet.matchId);

    if (localLinkSide == DuelLinkSide::INITIATOR) {
        LOG_W(TAG, "OFFER from %s while initiator: ROLE_MISMATCH", MacToString(senderMac));
        sendReply(senderMac, NodeNetworkCommand::ROLE_MISMATCH, offerMatchId, 0);
        return;
    }

    if (packet.senderIsInitiator != 1 || session.isActive() ||
        nodeNetworkPacketUuidIsZero(packet.senderPlayerId)) {
        LOG_W(TAG, "OFFER from %s dropped: invalid or session already active",
              MacToString(senderMac));
        return;
    }

    DuelParticipant local;
    if (!buildLocalParticipant(local)) {
        LOG_W(TAG, "OFFER dropped: local player not registered");
        return;
    }

    const int64_t localUnixMs = platformClock->unixMilliseconds();
    if (packet.occurredAtUnixMs <= 0 || localUnixMs <= 0) {
        LOG_W(TAG, "OFFER dropped: no valid unix time (offer=%lld local=%lld)",
              static_cast<long long>(packet.occurredAtUnixMs),
              static_cast<long long>(localUnixMs));
        return;
    }

    if (packet.occurredAtUnixMs < localUnixMs / TIME_IMPLAUSIBLE_RATIO) {
        LOG_I(TAG, "OFFER time implausible: TIME_CORRECTION -> %s", MacToString(senderMac));
        sendReply(senderMac, NodeNetworkCommand::TIME_CORRECTION, offerMatchId, localUnixMs);
        return;
    }

    DuelParticipant opponent;
    opponent.playerId = nodeNetworkPacketGetUuid(packet.senderPlayerId);
    opponent.teamId = nodeNetworkPacketGetUuid(packet.senderTeamId);

    trackPeer(senderMac);
    aborted = false;
    session.prime(DuelLinkSide::RESPONDER, offerMatchId, senderMac, local, packet.occurredAtUnixMs);
    session.setOpponentParticipant(opponent);
    session.markHandshakeComplete();
    LOG_I(TAG, "OFFER from %s accepted: handshake complete (responder)", MacToString(senderMac));
    sendReply(senderMac, NodeNetworkCommand::ACCEPT, offerMatchId, packet.occurredAtUnixMs);
}

bool MatchManagerV2::isOpponentSideSender(const NodeNetworkPacket& packet) const {
    const bool senderIsInitiator = packet.senderIsInitiator == 1;
    return senderIsInitiator == (session.getLocalSide() == DuelLinkSide::RESPONDER);
}

void MatchManagerV2::onAccept(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    const UUIDBytes acceptMatchId = nodeNetworkPacketGetUuid(packet.matchId);
    if (!session.isFromOpponent(senderMac, acceptMatchId) ||
        session.getLocalSide() != DuelLinkSide::INITIATOR || session.isHandshakeComplete() ||
        !isOpponentSideSender(packet) || nodeNetworkPacketUuidIsZero(packet.senderPlayerId)) {
        LOG_W(TAG, "ACCEPT from %s dropped", MacToString(senderMac));
        return;
    }

    DuelParticipant opponent;
    opponent.playerId = nodeNetworkPacketGetUuid(packet.senderPlayerId);
    opponent.teamId = nodeNetworkPacketGetUuid(packet.senderTeamId);
    session.setOpponentParticipant(opponent);
    session.markHandshakeComplete();
    LOG_I(TAG, "ACCEPT from %s: handshake complete (initiator)", MacToString(senderMac));
}

void MatchManagerV2::onTimeCorrection(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!session.isFromOpponent(senderMac, nodeNetworkPacketGetUuid(packet.matchId)) ||
        session.getLocalSide() != DuelLinkSide::INITIATOR || session.isHandshakeComplete() ||
        timeCorrectionApplied || packet.occurredAtUnixMs <= 0) {
        return;
    }

    timeCorrectionApplied = true;
    session.setOccurredAtUnixMs(packet.occurredAtUnixMs);
    LOG_I(TAG, "TIME_CORRECTION from %s: re-sending OFFER", MacToString(senderMac));
    sendOffer();
}

bool MatchManagerV2::isRoundPacketFromOpponent(const uint8_t* senderMac,
                                               const NodeNetworkPacket& packet) const {
    return session.isFromOpponent(senderMac, nodeNetworkPacketGetUuid(packet.matchId)) &&
           session.isHandshakeComplete() && isOpponentSideSender(packet);
}

void MatchManagerV2::onSync(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    const uint32_t receivedAtMs = nowMs();
    if (!isRoundPacketFromOpponent(senderMac, packet) ||
        session.getLocalSide() != DuelLinkSide::RESPONDER) {
        return;
    }
    NodeNetworkPacket reply = makeSessionPacket(NodeNetworkCommand::SYNC_REPLY);
    reply.syncOriginMs = packet.syncOriginMs;
    reply.syncReceiveMs = receivedAtMs;
    reply.syncTransmitMs = nowMs();
    sendToOpponent(reply);
}

void MatchManagerV2::onSyncReply(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    const uint32_t receivedAtMs = nowMs();
    if (!isRoundPacketFromOpponent(senderMac, packet) ||
        session.getLocalSide() != DuelLinkSide::INITIATOR || session.hasSequence()) {
        return;
    }
    const uint32_t elapsedMs = receivedAtMs - packet.syncOriginMs;
    const uint32_t responderHoldMs = packet.syncTransmitMs - packet.syncReceiveMs;
    if (responderHoldMs > elapsedMs) {
        LOG_W(TAG, "SYNC_REPLY dropped: responder hold %lu > round trip %lu",
              static_cast<unsigned long>(responderHoldMs), static_cast<unsigned long>(elapsedMs));
        return;
    }
    const int32_t outboundMs = static_cast<int32_t>(packet.syncReceiveMs - packet.syncOriginMs);
    const int32_t inboundMs = static_cast<int32_t>(packet.syncTransmitMs - receivedAtMs);
    const int32_t offsetMs =
    static_cast<int32_t>((static_cast<int64_t>(outboundMs) + inboundMs) / 2);
    const uint32_t roundTripMs = elapsedMs - responderHoldMs;
    session.recordSyncSample(offsetMs, roundTripMs);
    LOG_I(TAG, "SYNC_REPLY offset=%ld rtt=%lu", static_cast<long>(offsetMs),
          static_cast<unsigned long>(roundTripMs));
}

void MatchManagerV2::onSequence(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!isRoundPacketFromOpponent(senderMac, packet) ||
        session.getLocalSide() != DuelLinkSide::RESPONDER || session.hasSequence()) {
        return;
    }
    const int32_t leadMs = static_cast<int32_t>(packet.stepZeroAtMs - nowMs());
    if (leadMs <= 0 ||
        leadMs > static_cast<int32_t>(NODE_COUNTDOWN_MS) + NODE_SEQUENCE_LEAD_SLACK_MS) {
        LOG_W(TAG, "SEQUENCE rejected: step 0 in %ld ms", static_cast<long>(leadMs));
        abortRound();
        return;
    }
    NodeInputSequence sequence;
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        NodeInput& input = sequence.inputs[step];
        const uint8_t prompts = packet.stepPrompts[step];
        if (!decodePrompt(prompts & 0x0F, input.initiator) ||
            !decodePrompt(static_cast<uint8_t>(prompts >> PROMPT_NIBBLE_BITS), input.responder) ||
            input.initiator.count + input.responder.count == 0) {
            LOG_W(TAG, "SEQUENCE rejected: step %u prompts 0x%02x", step, prompts);
            abortRound();
            return;
        }
    }
    session.setSequence(sequence, packet.stepZeroAtMs);
    LOG_I(TAG, "SEQUENCE prompts=%02x %02x %02x %02x %02x step 0 in %ld ms", packet.stepPrompts[0],
          packet.stepPrompts[1], packet.stepPrompts[2], packet.stepPrompts[3],
          packet.stepPrompts[4], static_cast<long>(leadMs));
}

void MatchManagerV2::onInputResult(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!isRoundPacketFromOpponent(senderMac, packet) ||
        !session.isOpponentPresser(packet.stepIndex)) {
        return;
    }
    session.recordOpponentPresses(packet.stepIndex, packet.pressCount);
    if (!session.recordStepResult(packet.stepIndex, session.getOpponentSide(),
                                  packet.inputSuccess == 1)) {
        LOG_W(TAG, "INPUT_RESULT step=%u ignored: already resolved", packet.stepIndex);
    }
}

void MatchManagerV2::onInputPress(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!isRoundPacketFromOpponent(senderMac, packet) ||
        !session.isOpponentPresser(packet.stepIndex)) {
        return;
    }
    session.recordOpponentPresses(packet.stepIndex, packet.pressCount);
}

void MatchManagerV2::onOutcome(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!isRoundPacketFromOpponent(senderMac, packet) ||
        session.getLocalSide() != DuelLinkSide::RESPONDER || session.isOutcomeKnown() ||
        static_cast<size_t>(packet.initiatorScore + packet.responderScore) > NODE_SEQUENCE_LENGTH) {
        return;
    }
    session.setOutcome(packet.initiatorScore, packet.responderScore);
    LOG_I(TAG, "OUTCOME initiator=%u responder=%u", packet.initiatorScore, packet.responderScore);
}

void MatchManagerV2::onAbort(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!isRoundPacketFromOpponent(senderMac, packet)) {
        return;
    }
    LOG_W(TAG, "ABORT from %s: round not counted", MacToString(senderMac));
    aborted = true;
    clearCurrentMatch();
}

void MatchManagerV2::sendSyncProbe() {
    if (!session.isHandshakeComplete() || session.getLocalSide() != DuelLinkSide::INITIATOR) {
        return;
    }
    NodeNetworkPacket packet = makeSessionPacket(NodeNetworkCommand::SYNC);
    packet.syncOriginMs = nowMs();
    sendToOpponent(packet);
}

bool MatchManagerV2::hasClockSync() const {
    return session.hasSyncSample() && session.getSyncRoundTripMs() <= NODE_SYNC_MAX_RTT_MS;
}

bool MatchManagerV2::beginRound(const NodeInputSequence& sequence) {
    const uint32_t stepZeroLocalMs = nowMs() + static_cast<uint32_t>(NODE_COUNTDOWN_MS);
    if (!session.isHandshakeComplete() || session.getLocalSide() != DuelLinkSide::INITIATOR ||
        !hasClockSync() || !session.setSequence(sequence, stepZeroLocalMs)) {
        return false;
    }
    NodeNetworkPacket packet = makeSessionPacket(NodeNetworkCommand::SEQUENCE);
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        packet.stepPrompts[step] = static_cast<uint8_t>(
        encodePrompt(sequence.inputs[step].initiator) |
        encodePrompt(sequence.inputs[step].responder) << PROMPT_NIBBLE_BITS);
    }
    packet.stepZeroAtMs = stepZeroLocalMs + static_cast<uint32_t>(session.getSyncOffsetMs());
    LOG_I(TAG, "SEQUENCE -> %s offset=%ld", MacToString(session.getOpponentMac()),
          static_cast<long>(session.getSyncOffsetMs()));
    sendReliableToOpponent(packet);
    return true;
}

bool MatchManagerV2::hasSequence() const { return session.hasSequence(); }

int32_t MatchManagerV2::msUntilStep(uint8_t step) const {
    const uint32_t stepAtMs = session.getStepZeroAtMs() +
                              static_cast<uint32_t>(nodeStepOffsetMs(session.getSequence(), step));
    return static_cast<int32_t>(stepAtMs - nowMs());
}

int32_t MatchManagerV2::msUntilWindowCloses(uint8_t step) const {
    const unsigned long windowMs =
    step < NODE_SEQUENCE_LENGTH ? nodeStepWindowMs(session.getSequence().inputs[step]) : 0;
    return msUntilStep(step) + static_cast<int32_t>(windowMs);
}

bool MatchManagerV2::onLocalInputResult(uint8_t step, bool success, uint8_t pressCount) {
    if (!session.isHandshakeComplete() ||
        !session.recordStepResult(step, session.getLocalSide(), success)) {
        return false;
    }
    NodeNetworkPacket packet = makeSessionPacket(NodeNetworkCommand::INPUT_RESULT);
    packet.stepIndex = step;
    packet.inputSuccess = success ? 1 : 0;
    packet.pressCount = pressCount;
    sendReliableToOpponent(packet);
    return true;
}

void MatchManagerV2::onLocalPress(uint8_t step, uint8_t pressCount) {
    if (!session.isHandshakeComplete() || !session.isLocalPresser(step)) {
        return;
    }
    NodeNetworkPacket packet = makeSessionPacket(NodeNetworkCommand::INPUT_PRESS);
    packet.stepIndex = step;
    packet.pressCount = pressCount;
    sendToOpponent(packet);
}

void MatchManagerV2::expireStep(uint8_t step) {
    if (session.getLocalSide() != DuelLinkSide::INITIATOR || !session.hasSequence() ||
        step >= NODE_SEQUENCE_LENGTH || session.hasStepResult(step)) {
        return;
    }
    LOG_W(TAG, "step %u expired without a result", step);
    for (DuelLinkSide side : {DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER}) {
        session.recordStepResult(step, side, false);
    }
}

bool MatchManagerV2::finishRound() {
    if (!session.isHandshakeComplete() || session.getLocalSide() != DuelLinkSide::INITIATOR ||
        session.isOutcomeKnown()) {
        return false;
    }
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        expireStep(step);
    }
    session.setOutcome(session.countSuccesses(DuelLinkSide::INITIATOR),
                       session.countSuccesses(DuelLinkSide::RESPONDER));

    NodeNetworkPacket packet = makeSessionPacket(NodeNetworkCommand::OUTCOME);
    packet.initiatorScore = session.getInitiatorScore();
    packet.responderScore = session.getResponderScore();
    LOG_I(TAG, "OUTCOME -> %s initiator=%u responder=%u", MacToString(session.getOpponentMac()),
          packet.initiatorScore, packet.responderScore);
    sendReliableToOpponent(packet);
    return true;
}

void MatchManagerV2::abortRound() {
    if (session.isActive()) {
        LOG_W(TAG, "round aborted: not counted");
        sendReliableToOpponent(makeSessionPacket(NodeNetworkCommand::ABORT));
    }
    aborted = true;
    clearCurrentMatch();
}

bool MatchManagerV2::isAborted() const { return aborted; }

void MatchManagerV2::clearAbort() { aborted = false; }

bool MatchManagerV2::isHandshakeComplete() const { return session.isHandshakeComplete(); }

bool MatchManagerV2::isOutcomeKnown() const { return session.isOutcomeKnown(); }

void MatchManagerV2::concludeRound() {
    lastRoundScore = session.getBestKnownScore();
    lastRoundSuccess = session.isRoundSuccess();
    LOG_I(TAG, "round concluded: score=%u/%u success=%d outcomeKnown=%d", lastRoundScore,
          static_cast<unsigned>(NODE_SEQUENCE_LENGTH), lastRoundSuccess ? 1 : 0,
          session.isOutcomeKnown() ? 1 : 0);
    if (lastRoundSuccess) {
        finalizeMatch();
    }
    recordLinkOutcome(lastRoundSuccess);
    clearCurrentMatch();
}

uint8_t MatchManagerV2::getLastRoundScore() const { return lastRoundScore; }

bool MatchManagerV2::wasLastRoundSuccess() const { return lastRoundSuccess; }

bool MatchManagerV2::finalizeMatch() {
    if (!session.isRoundSuccess()) {
        LOG_W(TAG, "finalizeMatch: round did not succeed, nothing stored");
        return false;
    }
    QuickdrawMatchRecord record;
    if (!session.tryBuildUploadRecord(record)) {
        LOG_E(
        TAG,
        "finalizeMatch: cannot build upload record (handshake, outcome or player ids missing)");
        return false;
    }
    if (!matchStorage->append(record)) {
        LOG_E(TAG, "finalizeMatch: storing record failed (pending=%lu)",
              static_cast<unsigned long>(matchStorage->getCount()));
        return false;
    }

    LOG_I(TAG, "finalizeMatch: stored record, pending=%lu occurredAt=%lld",
          static_cast<unsigned long>(matchStorage->getCount()),
          static_cast<long long>(record.occurredAtUnixMs));
    clearCurrentMatch();
    return true;
}

void MatchManagerV2::recordLinkOutcome(bool success) {
    if (success) {
        failedAttempts = 0;
    } else {
        failedAttempts++;
    }
}

uint32_t MatchManagerV2::getFailedAttempts() const {
    return failedAttempts;
}

bool MatchManagerV2::hasExhaustedAttempts() const {
    return failedAttempts >= MAX_FAILED_ATTEMPTS;
}

uint32_t MatchManagerV2::getRetriesRemaining() const {
    if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
        return 0;
    }
    return MAX_FAILED_ATTEMPTS - failedAttempts;
}

DuelLinkSide MatchManagerV2::getLocalLinkSide() const {
    return localLinkSide;
}

void MatchManagerV2::clearFailedAttemptsForCurrentPeer() {
    failedAttempts = 0;
}

const uint8_t* MatchManagerV2::getTrackedPeerMac() const {
    return hasTrackedPeer ? trackedPeerMac : nullptr;
}

size_t MatchManagerV2::getPendingRecordCount() const {
    return matchStorage->getCount();
}

bool MatchManagerV2::readPendingRecord(size_t index, QuickdrawMatchRecord& record) const {
    return matchStorage->readAt(index, record);
}

void MatchManagerV2::clearPendingRecords() {
    matchStorage->clear();
}

void MatchManagerV2::clearCurrentMatch() {
    session.clear();
    timeCorrectionApplied = false;
}

const DuelSession& MatchManagerV2::getSession() const {
    return session;
}
