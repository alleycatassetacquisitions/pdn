#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/match-packet.hpp"
#include "game/posse-manager.hpp"
#include "game/shootout-manager.hpp"
#include "id-generator.hpp"
#include <algorithm>
#include <optional>
#include <cstring>

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

MatchManager::MatchManager(Player* player, StorageInterface* storage,
                           WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
    : player(player)
    , rdc(rdc)
    , storage(storage)
    , wirelessManager(wirelessManager)
    , matchChannel(wirelessManager, PktType::kMatchCommand) {
    matchChannel.onReceive([this](const uint8_t* fromMac, const MatchPacket& p) {
        MatchCommand command(fromMac, p.command, p.matchId, p.playerDrawTime, p.isHunter,
                             p.playerUuid, p.teamUuid);
        command.boostMs = p.boostMs;
        listenForMatchEvents(command);
    });
    // The bounty readies on the radio's SEND_SUCCESS for its MATCH_ID_ACK, not on
    // sending it, so a lost ack no longer starts its countdown ahead of the hunter's.
    matchChannel.setOnDelivered([this](uint8_t seqId, const uint8_t*) {
        if (activeMatchState.pendingAckSeqId == 0 || seqId != activeMatchState.pendingAckSeqId) {
            return;
        }
        activeMatchState.pendingAckSeqId = 0;
        activeMatchState.matchIsReady = true;
    });
    // No delivery report within the retransmit span: drop the match rather than
    // count down against a hunter that may never have heard the ack.
    matchChannel.setOnAbandon([this](uint8_t seqId, const uint8_t*, const MatchPacket&) {
        if (activeMatchState.pendingAckSeqId == 0 || seqId != activeMatchState.pendingAckSeqId) {
            return;
        }
        clearCurrentMatch();
    });
    if (this->wirelessManager) {
        this->wirelessManager->setEspNowPacketHandler(
        PktType::kMatchCollectionAck,
        [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<MatchManager*>(ctx)->onMatchRecordAck(fromMac, data, dataLen);
        },
        this);
    }
    initialize();
}

uint8_t MatchManager::sendCommand(const uint8_t* mac, int command, const char* matchId,
                                  long drawTime, unsigned long boostMs, bool isHunter) {
    MatchPacket packet{};
    packet.command = command;
    packet.playerDrawTime = drawTime;
    packet.boostMs = static_cast<uint32_t>(boostMs);
    packet.isHunter = isHunter;
    IdGenerator::copyId(packet.matchId, matchId);
    packet.playerUuid = player->getServerPlayerId();
    packet.teamUuid = player->getRegistrationTeam().id;

    LOG_I(MATCH_MANAGER_TAG, "Sending command %i to %s", command, MacToString(mac));
    return matchChannel.sendReliable(mac, packet);
}

MatchManager::~MatchManager() {
    if (wirelessManager) {
        wirelessManager->clearEspNowPacketHandler(PktType::kMatchCollectionAck);
    }
    if (storage) {
        storage->end();
    }
}

void MatchManager::setBoostProvider(std::function<unsigned long()> provider) {
    boostProvider = std::move(provider);
}

void MatchManager::setShootoutManager(ShootoutManager* manager) { shootoutManager = manager; }

bool MatchManager::currentMatchIsShootout() const {
    return activeMatchState.match.has_value() &&
           strncmp(activeMatchState.match->getMatchId(), kShootoutMatchIdPrefix,
                   sizeof(kShootoutMatchIdPrefix) - 1) == 0;
}

void MatchManager::clearShootoutMatch() {
    if (!currentMatchIsShootout()) return;
    clearCurrentMatch();
}

void MatchManager::clearCurrentMatch() {
    if (!activeMatchState.match) return;
    LOG_I(MATCH_MANAGER_TAG, "Clearing current match");
    // Keep matchChannel retrying only when a local DRAW_RESULT or NEVER_PRESSED was sent.
    if (!activeMatchState.hasPressedButton && !activeMatchState.gracePeriodExpiredNoResult) {
        matchChannel.cancel(activeMatchState.opponentMac.data());
    }
    activeMatchState = ActiveMatchState{};
}

void MatchManager::primeMatch(const char* matchId, const uint8_t* opponentMac,
                              bool localIsHunter) {
    activeMatchState.localIsHunter = localIsHunter;
    activeMatchState.match.emplace(matchId);
    memcpy(activeMatchState.opponentMac.data(), opponentMac, 6);

    Match& match = *activeMatchState.match;
    const UUIDBytes& myPlayerUuid = player->getServerPlayerId();
    const UUIDBytes& myTeamUuid = player->getRegistrationTeam().id;
    if (localIsHunter) {
        match.setHunterPlayerId(myPlayerUuid);
        match.setHunterTeamId(myTeamUuid);
    } else {
        match.setBountyPlayerId(myPlayerUuid);
        match.setBountyTeamId(myTeamUuid);
    }
}

void MatchManager::setOpponentIdentity(bool opponentIsHunter, const UUIDBytes& playerUuid,
                                       const UUIDBytes& teamUuid) {
    Match& match = *activeMatchState.match;
    if (opponentIsHunter) {
        match.setHunterPlayerId(playerUuid);
        match.setHunterTeamId(teamUuid);
    } else {
        match.setBountyPlayerId(playerUuid);
        match.setBountyTeamId(teamUuid);
    }
}

void MatchManager::initializeMatch(uint8_t* opponentMac) {
    if (activeMatchState.match.has_value()) {
        return;
    }

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING initializeMatch T=%lu", clock ? clock->milliseconds() : 0UL);

    primeMatch(IdGenerator::getInstance().generateId(), opponentMac, player->isHunter());
    sendCommand(activeMatchState.opponentMac.data(), MatchCmd::SEND_MATCH_ID,
                activeMatchState.match->getMatchId(), 0, 0, activeMatchState.localIsHunter);
}

void MatchManager::initializeShootoutMatch(const char* matchId, uint8_t* opponentMac,
                                           bool localIsHunter) {
    // A repeat MATCH_START names a match that may already be mounted; the live one
    // wins, since re-priming would reset a draw already in progress.
    if (activeMatchState.match.has_value()) return;

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING initializeShootoutMatch T=%lu",
          clock ? clock->milliseconds() : 0UL);

    primeMatch(matchId, opponentMac, localIsHunter);
    activeMatchState.matchIsReady = true;
}

void MatchManager::receiveMatch(const char* matchId, bool opponentIsHunter, uint8_t* opponentMac,
                                const UUIDBytes& opponentPlayerUuid,
                                const UUIDBytes& opponentTeamUuid) {
    primeMatch(matchId, opponentMac, player->isHunter());
    setOpponentIdentity(opponentIsHunter, opponentPlayerUuid, opponentTeamUuid);

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING receiveMatch-ackSent T=%lu",
          clock ? clock->milliseconds() : 0UL);

    activeMatchState.pendingAckSeqId =
    sendCommand(activeMatchState.opponentMac.data(), MatchCmd::MATCH_ID_ACK,
                activeMatchState.match->getMatchId(), 0, 0, activeMatchState.localIsHunter);
}

void MatchManager::setMatchLocalStartTime(unsigned long localStartTimeMs) {
    activeMatchState.matchLocalStartTime = localStartTimeMs;
}

unsigned long MatchManager::getMatchLocalStartTime() const {
    return activeMatchState.matchLocalStartTime;
}

bool MatchManager::matchResultsAreIn() {
    if (!activeMatchState.match) {
        return false;
    }

    // Final clause is the deadlock escape for both-timed-out matches where the
    // opponent's NEVER_PRESSED packet drops.
    return (activeMatchState.hasReceivedDrawResult && activeMatchState.hasPressedButton) ||
           (activeMatchState.opponentNeverPressed && activeMatchState.hasPressedButton) ||
           activeMatchState.gracePeriodExpiredNoResult;
}

void MatchManager::setNeverPressed() { activeMatchState.gracePeriodExpiredNoResult = true; }

bool MatchManager::didWin() {
    if (!activeMatchState.match) {
        return false;
    }

    if (activeMatchState.opponentNeverPressed) {
        return true;
    }
    if (activeMatchState.gracePeriodExpiredNoResult) {
        return false;
    }

    // An opponent whose result never arrived has no time to beat; a boosted
    // local time below zero must not win against the missing one.
    if (!activeMatchState.hasReceivedDrawResult) {
        return false;
    }
    return getEffectiveDrawTime(activeMatchState.localIsHunter) <
           getEffectiveDrawTime(!activeMatchState.localIsHunter);
}

bool MatchManager::finalizeMatch() {
    if (!activeMatchState.match) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    Match& match = *activeMatchState.match;
    const long hunterTime = getEffectiveDrawTime(true);
    const long bountyTime = getEffectiveDrawTime(false);

    // The whole result-screen snapshot is taken here, before clearCurrentMatch wipes the match.
    lastMatchDisplay.myTimeMs = activeMatchState.localIsHunter ? hunterTime : bountyTime;
    lastMatchDisplay.opponentTimeMs = activeMatchState.localIsHunter ? bountyTime : hunterTime;
    lastMatchDisplay.boostMs = activeMatchState.boostMs;
    lastMatchDisplay.penaltyMs = activeMatchState.penaltyMs;
    lastMatchDisplay.hasData = true;

    // The recorded winner must be one both devices arrive at, so it cannot come
    // from this device's own didWin(). Each knows which side never pressed, and
    // that outranks the times as it does on the result screen; otherwise the
    // faster effective draw wins, the same comparison didWin() makes. A tie,
    // neither side pressing, or an opponent result that never arrived leaves
    // nothing both can report, and the server requires a winner.
    const bool localNeverPressed = activeMatchState.gracePeriodExpiredNoResult;
    const bool opponentNeverPressed = activeMatchState.opponentNeverPressed;
    bool hunterWon = hunterTime < bountyTime;
    bool winnerIsShared = activeMatchState.hasReceivedDrawResult && hunterTime != bountyTime;
    if (localNeverPressed != opponentNeverPressed) {
        hunterWon = opponentNeverPressed == activeMatchState.localIsHunter;
        winnerIsShared = true;
    } else if (localNeverPressed) {
        winnerIsShared = false;
    }
    match.setWinnerPlayerId(hunterWon ? match.getHunterPlayerId() : match.getBountyPlayerId());
    if (PlatformClock* clock = SimpleTimer::getPlatformClock()) {
        match.setOccurredAtUnixMs(clock->unixMilliseconds());
    }

    std::string matchId = match.getMatchId();

    // Shootout matches are local-ephemeral: no save, no upload. Neither is a match
    // with a demo player, which has no server identity for the record to name.
    if (currentMatchIsShootout() || match.getHunterPlayerId().isZero() ||
        match.getBountyPlayerId().isZero() || !winnerIsShared) {
        clearCurrentMatch();
        return true;
    }

    if (appendMatchToStorage(&*activeMatchState.match)) {
        clearCurrentMatch();
        LOG_I(MATCH_MANAGER_TAG, "Successfully finalized match %s\n", matchId.c_str());
        return true;
    }

    LOG_E("PDN", "Failed to finalize match %s\n", matchId.c_str());
    return false;
}

bool MatchManager::getHasReceivedDrawResult() const {
    return activeMatchState.hasReceivedDrawResult;
}

bool MatchManager::getHasPressedButton() const { return activeMatchState.hasPressedButton; }

void MatchManager::setReceivedDrawResult() { activeMatchState.hasReceivedDrawResult = true; }

parameterizedCallbackFunction MatchManager::getMatchButtonPush() { return matchButtonPush; }

void MatchManager::setReceivedButtonPush() { activeMatchState.hasPressedButton = true; }

bool MatchManager::setHunterDrawTime(unsigned long hunterTimeMs) {
    if (!activeMatchState.match) {
        return false;
    }
    activeMatchState.match->setHunterDrawTime(hunterTimeMs);
    return true;
}

bool MatchManager::setBountyDrawTime(unsigned long bountyTimeMs) {
    if (!activeMatchState.match) {
        return false;
    }
    activeMatchState.match->setBountyDrawTime(bountyTimeMs);
    return true;
}

long MatchManager::getEffectiveDrawTime(bool hunterSlot) const {
    const Match& match = *activeMatchState.match;
    const unsigned long rawTimeMs =
    hunterSlot ? match.getHunterDrawTime() : match.getBountyDrawTime();
    const unsigned long boostMs = hunterSlot == activeMatchState.localIsHunter
                                  ? activeMatchState.boostMs
                                  : activeMatchState.opponentBoostMs;
    return static_cast<long>(rawTimeMs) - static_cast<long>(boostMs);
}

uint32_t MatchManager::getMatchSequenceNumber() {
    if (!storage) return 0;
    return storage->readUInt(MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, 0);
}

uint32_t MatchManager::getCollectedHighWaterMark() {
    if (!storage) return 0;
    return storage->readUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0);
}

uint32_t MatchManager::getOldestStoredSequence() {
    const uint32_t seq = getMatchSequenceNumber();
    return (seq > MATCH_RING_CAPACITY) ? (seq - MATCH_RING_CAPACITY + 1) : 1;
}

bool MatchManager::appendMatchToStorage(const Match* match) {
    if (!match || !storage) return false;

    const uint32_t nextSeq = getMatchSequenceNumber() + 1;
    char key[16];
    snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX,
             static_cast<unsigned>((nextSeq - 1) % MATCH_RING_CAPACITY));

    uint8_t buffer[MATCH_BINARY_SIZE] = {};
    match->serialize(buffer);

    if (storage->writeBytes(MATCHES_PREFS_NAMESPACE, key, buffer, MATCH_BINARY_SIZE) !=
        MATCH_BINARY_SIZE) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to save match binary at key %s", key);
        return false;
    }

    if (storage->writeUInt(MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, nextSeq) != sizeof(uint32_t)) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to update match sequence number to %u", nextSeq);
        return false;
    }

    return true;
}

bool MatchManager::readMatchBySequence(uint32_t seq, Match& out) {
    if (!storage || seq == 0 || seq > getMatchSequenceNumber() || seq < getOldestStoredSequence()) {
        return false;
    }

    char key[16];
    snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX,
             static_cast<unsigned>((seq - 1) % MATCH_RING_CAPACITY));

    uint8_t buffer[MATCH_BINARY_SIZE] = {};
    if (storage->readBytes(MATCHES_PREFS_NAMESPACE, key, buffer, MATCH_BINARY_SIZE) !=
        MATCH_BINARY_SIZE) {
        return false;
    }

    out.deserialize(buffer);
    return true;
}

void MatchManager::sync() {
    if (!storage || !wirelessManager) return;

    const uint8_t* fdnMac = rdc ? rdc->getConnectedPeerMac(DeviceType::FDN) : nullptr;
    if (!fdnMac) {
        transferWindowTimer.invalidate();
        sentRecordSeq = 0;
        return;
    }
    // NVS is polled and records sent at most once per window; an ack ends it early.
    if (transferWindowTimer.isRunning() && !transferWindowTimer.expired()) return;
    transferWindowTimer.setTimer(MATCH_TRANSFER_WINDOW_MS);

    const uint32_t collected = getCollectedHighWaterMark();
    if (collected >= getMatchSequenceNumber()) return;

    // Matches the ring already overwrote are gone; resume from the oldest still held.
    const uint32_t nextToSend = std::max(collected + 1, getOldestStoredSequence());

    MatchRecordPacket pkt{};
    pkt.matchSeq = nextToSend;
    Match match{};
    if (!readMatchBySequence(nextToSend, match)) {
        // An unreadable record would hold back every record after it.
        LOG_E(MATCH_MANAGER_TAG, "Skipping unreadable stored match %u", nextToSend);
        if (storage->writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, nextToSend) ==
            sizeof(uint32_t)) {
            transferWindowTimer.invalidate();
        }
        return;
    }
    match.serialize(pkt.matchData);

    sentRecordSeq = nextToSend;
    wirelessManager->sendEspNowData(fdnMac, PktType::kMatchCollection,
                                    reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

void MatchManager::onMatchRecordAck(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (!storage || !rdc || !rdc->isConnectedPeer(DeviceType::FDN, fromMac)) {
        return;
    }
    if (!data || dataLen != sizeof(MatchRecordAckPacket)) {
        LOG_W(MATCH_MANAGER_TAG, "Dropping match ack of %zu bytes", dataLen);
        return;
    }

    MatchRecordAckPacket ack{};
    memcpy(&ack, data, sizeof(ack));

    // Only the record just sent can be acked; anything else would mark records
    // the FDN was never given as collected.
    if (sentRecordSeq == 0 || ack.matchSeq != sentRecordSeq) {
        return;
    }
    if (storage->writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, ack.matchSeq) !=
        sizeof(uint32_t)) {
        return;
    }
    sentRecordSeq = 0;
    transferWindowTimer.invalidate();
}

parameterizedCallbackFunction MatchManager::getButtonMasher() {
    return buttonMasher;
}

void MatchManager::clearButtonMasherCount() { activeMatchState.buttonMasherCount = 0; }

void MatchManager::initialize() {
    // An NVS wipe of "seq" alone would otherwise leave every new match marked collected.
    if (getCollectedHighWaterMark() > getMatchSequenceNumber()) {
        storage->writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY,
                           getMatchSequenceNumber());
    }

    // Firmware before the ring kept JSON strings under these keys. Nothing reads
    // them now, and left in place they take the room the ring needs.
    if (getMatchSequenceNumber() == 0) {
        storage->clear(MATCHES_PREFS_NAMESPACE);
    }

    matchButtonPush = [](void* ctx) {
        if (!ctx) {
            LOG_E(MATCH_MANAGER_TAG, "Button press handler received null context");
            return;
        }

        auto* clock = SimpleTimer::getPlatformClock();
        if (!clock) {
            LOG_E(MATCH_MANAGER_TAG, "Button press handler called with no platform clock");
            return;
        }
        unsigned long now = clock->milliseconds();

        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        ActiveMatchState* activeMatchState = &matchManager->activeMatchState;
        Player* player = matchManager->player;

        if(matchManager->getHasPressedButton()) {
            LOG_I(MATCH_MANAGER_TAG, "Button already pressed - skipping");
            return;
        }

        if(!matchManager->getCurrentMatch().has_value()) {
            LOG_E(MATCH_MANAGER_TAG, "No current match - skipping");
            return;
        }

        unsigned long reactionTimeMs = now - matchManager->getMatchLocalStartTime();

        const unsigned long penaltyMs = activeMatchState->calculateButtonMasherPenalty();
        reactionTimeMs += penaltyMs;

        // Held to a full posse's worth, the same bound the opponent applies to
        // the boost it reads off the frame, so both compare the same numbers.
        const unsigned long boost =
        std::min(matchManager->boostProvider ? matchManager->boostProvider() : 0UL,
                 PosseManager::getMaxBoostMs());

        // Snapshot boost at press time so the result screen can display it
        // even if supporters unplug before the match finalizes.
        activeMatchState->boostMs = boost;
        activeMatchState->penaltyMs = penaltyMs;

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms (boost %lu) for %s",
              reactionTimeMs, boost, activeMatchState->localIsHunter ? "Hunter" : "Bounty");

        // The record keeps the raw time; the boost is applied when times are compared.
        activeMatchState->localIsHunter ? matchManager->setHunterDrawTime(reactionTimeMs)
                                        : matchManager->setBountyDrawTime(reactionTimeMs);

        // Player stats record the raw (unboosted) reaction time — boost is a
        // match-time advantage, not an achievement the player actually made.
        player->addReactionTime(reactionTimeMs);

        LOG_I(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        LOG_I(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s",
              MacToString(activeMatchState->opponentMac.data()));

        // The raw time and the boost go out separately: the opponent's copy of
        // the record needs the first, and its comparison needs both.
        matchManager->sendCommand(activeMatchState->opponentMac.data(), MatchCmd::DRAW_RESULT,
                                  matchManager->getCurrentMatch()->getMatchId(), reactionTimeMs,
                                  boost, activeMatchState->localIsHunter);

        matchManager->setReceivedButtonPush();
    };

    buttonMasher = [](void *ctx) {
        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        matchManager->activeMatchState.buttonMasherCount++;
        LOG_I(MATCH_MANAGER_TAG, "Button masher count: %d",
              matchManager->activeMatchState.buttonMasherCount);
    };
}

bool MatchManager::isFromActiveMatchOpponent(const MatchCommand& command) const {
    if (!activeMatchState.match.has_value()) return false;
    if (memcmp(command.wifiMacAddr, activeMatchState.opponentMac.data(), 6) != 0) return false;
    if (strcmp(command.matchId, activeMatchState.match->getMatchId()) != 0) return false;
    return true;
}

void MatchManager::listenForMatchEvents(const MatchCommand& command) {
    LOG_I(MATCH_MANAGER_TAG, "Received command: %d", command.command);

    if (command.command == MatchCmd::SEND_MATCH_ID) {
        LOG_I(MATCH_MANAGER_TAG, "Received SEND_MATCH_ID command from opponent");
        // One match at a time, and a tournament pairs its own players.
        if (activeMatchState.match.has_value()) return;
        if (shootoutManager && shootoutManager->active()) return;
        // Gate on RDC peering — no active match exists yet, so the sender
        // must be authenticated via the cable-established direct peer.
        if (!rdc || !rdc->isDirectPeer(command.wifiMacAddr)) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: sender not an RDC direct peer");
            return;
        }
        if (!player->getServerPlayerId().isZero() &&
            command.playerUuid == player->getServerPlayerId()) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: self playerUuid");
            return;
        }
        if (player->isHunter() == command.isHunter) {
            // Echo the sender's matchId/MAC: activeMatchState.match is still empty.
            sendCommand(command.wifiMacAddr, MatchCmd::MATCH_ROLE_MISMATCH, command.matchId, 0, 0,
                        player->isHunter());
            return;
        }
        receiveMatch(command.matchId, command.isHunter, const_cast<uint8_t*>(command.wifiMacAddr),
                     command.playerUuid, command.teamUuid);
    } else if (command.command == MatchCmd::MATCH_ID_ACK) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ID_ACK command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        if (activeMatchState.matchIsReady || activeMatchState.pendingAckSeqId != 0) return;
        matchChannel.cancel(activeMatchState.opponentMac.data());
        setOpponentIdentity(!activeMatchState.localIsHunter, command.playerUuid, command.teamUuid);
        auto* clock = SimpleTimer::getPlatformClock();
        LOG_D(MATCH_MANAGER_TAG, "TIMING matchAck-ready T=%lu", clock ? clock->milliseconds() : 0UL);
        activeMatchState.matchIsReady = true;
    } else if (command.command == MatchCmd::MATCH_ROLE_MISMATCH) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ROLE_MISMATCH command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        if (activeMatchState.matchIsReady || activeMatchState.pendingAckSeqId != 0) return;
        clearCurrentMatch();
        return;
    } else if (command.command == MatchCmd::DRAW_RESULT ||
               command.command == MatchCmd::NEVER_PRESSED) {
        LOG_I(MATCH_MANAGER_TAG, "Received DRAW_RESULT command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        // An opponent's frame naming this device's slot would overwrite the
        // local time and so pick the winner.
        if (command.isHunter == activeMatchState.localIsHunter) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting draw result: names the local draw slot");
            return;
        }
        // Not a reaction time, and as a signed effective time it could beat
        // any honest draw.
        if (command.playerDrawTime < 0) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting draw result: negative draw time");
            return;
        }

        const long opponentTime = command.playerDrawTime;
        // No frame earns more than a full posse can give.
        activeMatchState.opponentBoostMs = std::min(command.boostMs, PosseManager::getMaxBoostMs());

        LOG_I(MATCH_MANAGER_TAG, "Opponent reaction time: %ld ms (boost %lu)", opponentTime,
              activeMatchState.opponentBoostMs);

        command.isHunter ? setHunterDrawTime(opponentTime) : setBountyDrawTime(opponentTime);

        if (command.command == MatchCmd::NEVER_PRESSED) {
            activeMatchState.opponentNeverPressed = true;
        }

        setReceivedDrawResult();
    } else {
        LOG_W(MATCH_MANAGER_TAG, "Received unexpected command in Match Manager: %d", command.command);
    }
}

void MatchManager::sendNeverPressed(unsigned long pityTime) {
    if (!activeMatchState.match.has_value()) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot send NEVER_PRESSED - no active match");
        return;
    }

    activeMatchState.localIsHunter ? setHunterDrawTime(pityTime) : setBountyDrawTime(pityTime);
    setNeverPressed();
    // Mirror what gets uploaded: every match contributes a draw time to the server,
    // so on-device "average reaction" should include pity times too.
    player->addReactionTime(pityTime);

    sendCommand(activeMatchState.opponentMac.data(), MatchCmd::NEVER_PRESSED,
                activeMatchState.match->getMatchId(), pityTime, 0, activeMatchState.localIsHunter);
}

bool MatchManager::isMatchReady() const { return activeMatchState.matchIsReady; }