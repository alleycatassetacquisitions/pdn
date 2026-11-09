#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/match-packet.hpp"
#include "game/shootout-manager.hpp"
#include "id-generator.hpp"
#include <algorithm>
#include <optional>
#include <cstring>

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

MatchManager::MatchManager(WirelessManager* wirelessManager)
    : player(nullptr)
    , storage(nullptr)
    , wirelessManager(wirelessManager)
    , matchChannel(wirelessManager, PktType::kMatchCommand) {
    matchChannel.onReceive([this](const uint8_t* fromMac, const MatchPacket& p) {
        UUIDBytes playerUuid{};
        UUIDBytes teamUuid{};
        memcpy(playerUuid.bytes, p.playerUuid, UUIDBytes::SIZE);
        memcpy(teamUuid.bytes, p.teamUuid, UUIDBytes::SIZE);
        MatchCommand command(fromMac, p.command, p.matchId, p.playerId, p.playerDrawTime,
                             p.isHunter, playerUuid, teamUuid);
        listenForMatchEvents(command);
    });
    if (this->wirelessManager) {
        this->wirelessManager->setEspNowPacketHandler(
        PktType::kMatchCollectionAck,
        [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<MatchManager*>(ctx)->onMatchRecordAck(fromMac, data, dataLen);
        },
        this);
    }
}

void MatchManager::sync() { syncFdnMatchTransfer(); }

void MatchManager::sendCommand(const uint8_t* mac, int command, const char* matchId,
                               long drawTime, bool isHunter) {
    MatchPacket packet{};
    packet.command = command;
    packet.playerDrawTime = drawTime;
    packet.isHunter = isHunter;
    IdGenerator::copyId(packet.matchId, matchId);
    IdGenerator::copyId(packet.playerId, player->getUserID().c_str());
    memcpy(packet.playerUuid, player->getServerPlayerId().bytes, UUIDBytes::SIZE);
    memcpy(packet.teamUuid, player->getRegistrationTeam().id.bytes, UUIDBytes::SIZE);

    LOG_I(MATCH_MANAGER_TAG, "Sending command %i to %s", command, MacToString(mac));
    matchChannel.sendReliable(mac, packet);
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
    boostProvider_ = provider;
}

void MatchManager::setRemoteDeviceCoordinator(RemoteDeviceCoordinator* rdc) { this->rdc = rdc; }

void MatchManager::setShootoutManager(ShootoutManager* shootoutManager) {
    shootoutManager_ = shootoutManager;
}

ShootoutManager* MatchManager::getShootoutManager() const {
    return shootoutManager_;
}

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
    activeMatchState.match.emplace(matchId, player->getUserID().c_str(), localIsHunter);
    memcpy(activeMatchState.opponentMac.data(), opponentMac, 6);

    Match& match = *activeMatchState.match;
    const UUIDBytes& myPlayerUuid = player->getServerPlayerId();
    const UUIDBytes& myTeamUuid = player->getRegistrationTeam().id;
    const uint8_t* myMac = wirelessManager ? wirelessManager->getMacAddress() : nullptr;
    if (localIsHunter) {
        match.setHunterPlayerId(myPlayerUuid);
        match.setHunterTeamId(myTeamUuid);
        match.setHunterMac(myMac);
        match.setBountyMac(opponentMac);
    } else {
        match.setBountyPlayerId(myPlayerUuid);
        match.setBountyTeamId(myTeamUuid);
        match.setBountyMac(myMac);
        match.setHunterMac(opponentMac);
    }
}

void MatchManager::setOpponentIdentity(bool opponentIsHunter, const char* opponentId,
                                       const UUIDBytes& playerUuid, const UUIDBytes& teamUuid) {
    Match& match = *activeMatchState.match;
    if (opponentIsHunter) {
        match.setHunterId(opponentId);
        match.setHunterPlayerId(playerUuid);
        match.setHunterTeamId(teamUuid);
    } else {
        match.setBountyId(opponentId);
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
                activeMatchState.match->getMatchId(), 0, activeMatchState.localIsHunter);
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

void MatchManager::receiveMatch(const char* matchId, const char* opponentId, bool opponentIsHunter,
                                uint8_t* opponentMac, const UUIDBytes& opponentPlayerUuid,
                                const UUIDBytes& opponentTeamUuid) {
    if (activeMatchState.match.has_value()) {
        return;
    }

    primeMatch(matchId, opponentMac, player->isHunter());
    setOpponentIdentity(opponentIsHunter, opponentId, opponentPlayerUuid, opponentTeamUuid);

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING receiveMatch-ready T=%lu", clock ? clock->milliseconds() : 0UL);

    activeMatchState.matchIsReady = true;

    sendCommand(activeMatchState.opponentMac.data(), MatchCmd::MATCH_ID_ACK,
                activeMatchState.match->getMatchId(), 0, activeMatchState.localIsHunter);
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
    // opponent's NEVER_PRESSED packet drops. Gated on !hasPressedButton so a
    // button-press in the same tick as grace expiry still takes the press
    // path (clauses 1/2) — otherwise didWin() would spuriously return false.
    return (activeMatchState.hasReceivedDrawResult && activeMatchState.hasPressedButton) ||
           (activeMatchState.opponentNeverPressed && activeMatchState.hasPressedButton) ||
           (activeMatchState.gracePeriodExpiredNoResult && !activeMatchState.hasPressedButton);
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

    const unsigned long hunterTime = activeMatchState.match->getHunterDrawTime();
    const unsigned long bountyTime = activeMatchState.match->getBountyDrawTime();
    return activeMatchState.localIsHunter ? hunterTime < bountyTime : bountyTime < hunterTime;
}

bool MatchManager::finalizeMatch() {
    if (!activeMatchState.match) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    // Snapshot times for the result screen (before clearCurrentMatch wipes the match).
    lastMatchDisplay.myTimeMs = activeMatchState.localIsHunter
                                ? activeMatchState.match->getHunterDrawTime()
                                : activeMatchState.match->getBountyDrawTime();
    lastMatchDisplay.opponentTimeMs = activeMatchState.localIsHunter
                                      ? activeMatchState.match->getBountyDrawTime()
                                      : activeMatchState.match->getHunterDrawTime();
    lastMatchDisplay.hasData = true;

    const bool localWon = didWin();
    const bool hunterWon = activeMatchState.localIsHunter ? localWon : !localWon;
    activeMatchState.match->setWinnerPlayerId(hunterWon
                                              ? activeMatchState.match->getHunterPlayerId()
                                              : activeMatchState.match->getBountyPlayerId());
    if (PlatformClock* clock = SimpleTimer::getPlatformClock()) {
        activeMatchState.match->setOccurredAtUnixMs(clock->unixMilliseconds());
    }

    std::string matchId = activeMatchState.match->getMatchId();

    // Shootout matches are local-ephemeral: no save, no upload. Neither is a match
    // with a demo player, which has no server identity for the record to name.
    if (currentMatchIsShootout() || activeMatchState.match->getHunterPlayerId().isZero() ||
        activeMatchState.match->getBountyPlayerId().isZero()) {
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

void MatchManager::syncFdnMatchTransfer() {
    if (!storage || !wirelessManager) return;

    const uint8_t* fdnMac = rdc ? rdc->getConnectedPeerMac(DeviceType::FDN) : nullptr;
    if (!fdnMac) {
        transferWindowTimer.invalidate();
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

    if (ack.matchSeq <= getCollectedHighWaterMark() || ack.matchSeq > getMatchSequenceNumber()) {
        return;
    }
    storage->writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, ack.matchSeq);
    transferWindowTimer.invalidate();
}

parameterizedCallbackFunction MatchManager::getButtonMasher() {
    return buttonMasher;
}

void MatchManager::initialize(Player* player, StorageInterface* storage) {
    this->player = player;
    this->storage = storage;
    // An NVS wipe of "seq" alone would otherwise leave every new match marked collected.
    if (storage && getCollectedHighWaterMark() > getMatchSequenceNumber()) {
        storage->writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY,
                           getMatchSequenceNumber());
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

        if (activeMatchState->buttonMasherCount > 0) {
            reactionTimeMs = reactionTimeMs + activeMatchState->calculateButtonMasherPenalty();
        }

        unsigned long boost = matchManager->boostProvider_ ? matchManager->boostProvider_() : 0;
        unsigned long boostedTimeMs = (boost >= reactionTimeMs) ? 0 : (reactionTimeMs - boost);

        // Snapshot boost at press time so the result screen can display it
        // even if supporters unplug before the match finalizes.
        matchManager->lastMatchDisplay.boostMs = boost;

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms (boost %lu) for %s",
              boostedTimeMs, boost, activeMatchState->localIsHunter ? "Hunter" : "Bounty");

        activeMatchState->localIsHunter ? matchManager->setHunterDrawTime(boostedTimeMs)
                                        : matchManager->setBountyDrawTime(boostedTimeMs);

        // Player stats record the raw (unboosted) reaction time — boost is a
        // match-time advantage, not an achievement the player actually made.
        player->addReactionTime(reactionTimeMs);

        LOG_I(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        LOG_I(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s",
              MacToString(activeMatchState->opponentMac.data()));

        // Send the BOOSTED time in DRAW_RESULT so both sides agree on who won.
        matchManager->sendCommand(activeMatchState->opponentMac.data(), MatchCmd::DRAW_RESULT,
                                  matchManager->getCurrentMatch()->getMatchId(), boostedTimeMs,
                                  activeMatchState->localIsHunter);

        matchManager->setReceivedButtonPush();
        
        LOG_I(MATCH_MANAGER_TAG, "Reaction time: %lu ms", reactionTimeMs);
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
        // Gate on RDC peering — no active match exists yet, so the sender
        // must be authenticated via the cable-established direct peer.
        LOG_I(MATCH_MANAGER_TAG, "Received SEND_MATCH_ID command from opponent");
        if (!rdc || !rdc->isDirectPeer(command.wifiMacAddr)) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: sender not an RDC direct peer");
            return;
        }
        if (!player->getServerPlayerId().isZero() &&
            command.playerUuid == player->getServerPlayerId()) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: self playerUuid");
            return;
        }
        bool shootoutActive = shootoutManager_ && shootoutManager_->active();
        if(!shootoutActive && player->isHunter() == command.isHunter) {
            // Echo the sender's matchId/MAC: activeMatchState.match is still empty.
            sendCommand(command.wifiMacAddr, MatchCmd::MATCH_ROLE_MISMATCH, command.matchId, 0,
                        player->isHunter());
            return;
        } else {
            receiveMatch(command.matchId, command.playerId, command.isHunter,
                         const_cast<uint8_t*>(command.wifiMacAddr), command.playerUuid,
                         command.teamUuid);
        }
    } else if (command.command == MatchCmd::MATCH_ID_ACK) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ID_ACK command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        matchChannel.cancel(activeMatchState.opponentMac.data());
        setOpponentIdentity(!activeMatchState.localIsHunter, command.playerId, command.playerUuid,
                            command.teamUuid);
        auto* clock = SimpleTimer::getPlatformClock();
        LOG_D(MATCH_MANAGER_TAG, "TIMING matchAck-ready T=%lu", clock ? clock->milliseconds() : 0UL);
        activeMatchState.matchIsReady = true;
    } else if (command.command == MatchCmd::MATCH_ROLE_MISMATCH) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ROLE_MISMATCH command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        clearCurrentMatch();
        return;
    } else if (command.command == MatchCmd::DRAW_RESULT ||
               command.command == MatchCmd::NEVER_PRESSED) {
        LOG_I(MATCH_MANAGER_TAG, "Received DRAW_RESULT command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;

        long opponentTime = command.playerDrawTime;

        LOG_I(MATCH_MANAGER_TAG, "Opponent reaction time: %ld ms", opponentTime);

        command.isHunter ?
        setHunterDrawTime(opponentTime)
        : setBountyDrawTime(opponentTime);

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
                activeMatchState.match->getMatchId(), pityTime, activeMatchState.localIsHunter);
}

bool MatchManager::isMatchReady() const { return activeMatchState.matchIsReady; }