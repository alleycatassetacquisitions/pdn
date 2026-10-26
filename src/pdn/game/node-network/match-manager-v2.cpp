#include "game/node-network/match-manager-v2.hpp"

#include <cstring>

#include "id-generator.hpp"
#define TAG "MMV2"
#include "device/drivers/logger.hpp"

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
    packet.command = command;
    packet.senderIsInitiator = localLinkSide == DuelLinkSide::INITIATOR ? 1 : 0;
    packet.matchId = matchId;
    packet.occurredAtUnixMs = unixMs;
    DuelParticipant local;
    if (buildLocalParticipant(local)) {
        packet.senderPlayerId = local.playerId;
        packet.senderTeamId = local.teamId;
    }
    nodeNetworkWirelessManager->sendPacket(destinationMac, packet);
}

void MatchManagerV2::sendOffer() {
    sendReply(session.getOpponentMac(), NodeNetworkCommand::OFFER, session.getMatchId(),
              session.getOccurredAtUnixMs());
}

void MatchManagerV2::sendToOpponent(NodeNetworkCommand command, uint64_t drawTimeMs) {
    NodeNetworkPacket packet;
    packet.command = command;
    packet.senderIsInitiator = session.getLocalSide() == DuelLinkSide::INITIATOR ? 1 : 0;
    packet.matchId = session.getMatchId();
    packet.senderPlayerId = session.getLocalParticipant().playerId;
    packet.senderTeamId = session.getLocalParticipant().teamId;
    packet.drawTimeMs = drawTimeMs;
    packet.occurredAtUnixMs = session.getOccurredAtUnixMs();
    nodeNetworkWirelessManager->sendPacket(session.getOpponentMac(), packet);
}

bool MatchManagerV2::startMatch(const uint8_t* opponentMac) {
    if (localLinkSide != DuelLinkSide::INITIATOR || session.isActive()) {
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
    sendOffer();
    return true;
}

void MatchManagerV2::onPacket(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    switch (packet.command) {
        case NodeNetworkCommand::OFFER:
            onOffer(senderMac, packet);
            break;
        case NodeNetworkCommand::ACCEPT:
            onAccept(senderMac, packet);
            break;
        case NodeNetworkCommand::ROLE_MISMATCH:
            if (session.isFromOpponent(senderMac, packet.matchId)) {
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
    if (!remoteDeviceCoordinator->isDirectPeer(senderMac)) {
        return;
    }

    if (localLinkSide == DuelLinkSide::INITIATOR) {
        sendReply(senderMac, NodeNetworkCommand::ROLE_MISMATCH, packet.matchId, 0);
        return;
    }

    if (packet.senderIsInitiator != 1 || session.isActive() || packet.senderPlayerId.isZero()) {
        return;
    }

    DuelParticipant local;
    if (!buildLocalParticipant(local)) {
        return;
    }

    const int64_t localUnixMs = platformClock->unixMilliseconds();
    if (packet.occurredAtUnixMs <= 0 || localUnixMs <= 0) {
        return;
    }

    if (packet.occurredAtUnixMs < localUnixMs / TIME_IMPLAUSIBLE_RATIO) {
        sendReply(senderMac, NodeNetworkCommand::TIME_CORRECTION, packet.matchId, localUnixMs);
        return;
    }

    DuelParticipant opponent;
    opponent.playerId = packet.senderPlayerId;
    opponent.teamId = packet.senderTeamId;

    trackPeer(senderMac);
    session.prime(DuelLinkSide::RESPONDER, packet.matchId, senderMac, local,
                  packet.occurredAtUnixMs);
    session.setOpponentParticipant(opponent);
    session.markHandshakeComplete();
    sendReply(senderMac, NodeNetworkCommand::ACCEPT, packet.matchId, packet.occurredAtUnixMs);
}

bool MatchManagerV2::isOpponentSideSender(const NodeNetworkPacket& packet) const {
    const bool senderIsInitiator = packet.senderIsInitiator == 1;
    return senderIsInitiator == (session.getLocalSide() == DuelLinkSide::RESPONDER);
}

void MatchManagerV2::onAccept(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!session.isFromOpponent(senderMac, packet.matchId) ||
        session.getLocalSide() != DuelLinkSide::INITIATOR || session.isHandshakeComplete() ||
        !isOpponentSideSender(packet) || packet.senderPlayerId.isZero()) {
        return;
    }

    DuelParticipant opponent;
    opponent.playerId = packet.senderPlayerId;
    opponent.teamId = packet.senderTeamId;
    session.setOpponentParticipant(opponent);
    session.markHandshakeComplete();
}

void MatchManagerV2::onTimeCorrection(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!session.isFromOpponent(senderMac, packet.matchId) ||
        session.getLocalSide() != DuelLinkSide::INITIATOR || session.isHandshakeComplete() ||
        timeCorrectionApplied || packet.occurredAtUnixMs <= 0) {
        return;
    }

    timeCorrectionApplied = true;
    session.setOccurredAtUnixMs(packet.occurredAtUnixMs);
    sendOffer();
}

void MatchManagerV2::onDraw(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!session.isFromOpponent(senderMac, packet.matchId) || !session.isHandshakeComplete() ||
        !isOpponentSideSender(packet)) {
        return;
    }
    session.recordRemoteDrawTimeMs(packet.drawTimeMs);
}

void MatchManagerV2::onNeverPressed(const uint8_t* senderMac, const NodeNetworkPacket& packet) {
    if (!session.isFromOpponent(senderMac, packet.matchId) || !session.isHandshakeComplete() ||
        !isOpponentSideSender(packet)) {
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
        return false;
    }
    QuickdrawMatchRecord record;
    if (!session.tryBuildUploadRecord(record) || !matchStorage->append(record)) {
        return false;
    }
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
