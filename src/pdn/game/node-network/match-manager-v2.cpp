#include "game/node-network/match-manager-v2.hpp"

#include <cstring>

#include "id-generator.hpp"
#define TAG "MMV2"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"

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

void MatchManagerV2::sendToOpponent(NodeNetworkCommand command, uint64_t drawTimeMs) {
    NodeNetworkPacket packet;
    packet.command = static_cast<uint8_t>(command);
    packet.senderIsInitiator = session.getLocalSide() == DuelLinkSide::INITIATOR ? 1 : 0;
    nodeNetworkPacketSetUuid(packet.matchId, session.getMatchId());
    nodeNetworkPacketSetUuid(packet.senderPlayerId, session.getLocalParticipant().playerId);
    nodeNetworkPacketSetUuid(packet.senderTeamId, session.getLocalParticipant().teamId);
    packet.drawTimeMs = drawTimeMs;
    packet.occurredAtUnixMs = session.getOccurredAtUnixMs();
    nodeNetworkWirelessManager->sendPacket(session.getOpponentMac(), packet);
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
        case NodeNetworkCommand::DRAW:
            onDraw(senderMac, packet);
            break;
        case NodeNetworkCommand::NEVER_PRESSED:
            onNeverPressed(senderMac, packet);
            break;
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

void MatchManagerV2::onDraw(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!session.isFromOpponent(senderMac, nodeNetworkPacketGetUuid(packet.matchId)) ||
        !session.isHandshakeComplete() || !isOpponentSideSender(packet)) {
        return;
    }
    session.recordRemoteDrawTimeMs(packet.drawTimeMs);
}

void MatchManagerV2::onNeverPressed(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!session.isFromOpponent(senderMac, nodeNetworkPacketGetUuid(packet.matchId)) ||
        !session.isHandshakeComplete() || !isOpponentSideSender(packet)) {
        return;
    }
    if (session.recordRemoteDrawTimeMs(packet.drawTimeMs)) {
        session.setOpponentNeverPressed();
    }
}

bool MatchManagerV2::onLocalDrawTimeMs(uint64_t drawTimeMs) {
    if (!session.isHandshakeComplete() || !session.recordLocalDrawTimeMs(drawTimeMs)) {
        return false;
    }
    sendToOpponent(NodeNetworkCommand::DRAW, drawTimeMs);
    return true;
}

bool MatchManagerV2::onLocalGraceExpired(uint64_t elapsedMs) {
    if (!session.isHandshakeComplete() || session.hasLocalDrawTime() ||
        session.didGraceExpireWithoutPress()) {
        return false;
    }
    session.setGraceExpiredWithoutPress();
    sendToOpponent(NodeNetworkCommand::NEVER_PRESSED, elapsedMs);
    return true;
}

bool MatchManagerV2::isHandshakeComplete() const {
    return session.isHandshakeComplete();
}

bool MatchManagerV2::isExchangeComplete() const {
    return session.isExchangeComplete();
}

bool MatchManagerV2::didSync() const {
    if (!session.isActive() || !session.isHandshakeComplete() ||
        !session.hasInitiatorDrawTime() || !session.hasResponderDrawTime() ||
        session.didOpponentNeverPress() || session.didGraceExpireWithoutPress()) {
        return false;
    }
    const uint64_t initiatorMs = session.getInitiatorDrawTimeMs();
    const uint64_t responderMs = session.getResponderDrawTimeMs();
    const uint64_t delta =
        initiatorMs > responderMs ? initiatorMs - responderMs : responderMs - initiatorMs;
    return delta <= NODE_SYNC_WINDOW_MS;
}

bool MatchManagerV2::finalizeMatch() {
    if (!didSync()) {
        LOG_W(TAG, "finalizeMatch: duel did not sync, nothing stored");
        return false;
    }
    QuickdrawMatchRecord record;
    if (!session.tryBuildUploadRecord(record)) {
        LOG_E(TAG, "finalizeMatch: cannot build upload record (handshake, times or player ids missing)");
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
