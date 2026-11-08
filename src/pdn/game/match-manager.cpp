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
    , resender(wirelessManager)
    , matchChannel(wirelessManager, &resender, PktType::kMatchCommand, nullptr) {
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
        PktType::kMatchCollection,
        [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<MatchManager*>(ctx)->onMatchCollectionPacket(fromMac, data, dataLen);
        },
        this);
    }
}

void MatchManager::sync() {
    resender.sync();
    syncFdnMatchTransfer();
}

void MatchManager::sendCommand(const uint8_t* mac, int command, const char* matchId,
                               long drawTime, bool isHunter) {
    MatchPacket packet{};
    packet.command = command;
    packet.playerDrawTime = drawTime;
    packet.isHunter = isHunter;
    IdGenerator::copyId(packet.matchId, matchId);
    if (player) {
        IdGenerator::copyId(packet.playerId, player->getUserID().c_str());
        memcpy(packet.playerUuid, player->getServerPlayerId().bytes, UUIDBytes::SIZE);
        memcpy(packet.teamUuid, player->getRegistrationTeam().id.bytes, UUIDBytes::SIZE);
    }

    LOG_I(MATCH_MANAGER_TAG, "Sending command %i to %s", command, MacToString(mac));
    matchChannel.sendReliable(mac, packet);
}

MatchManager::~MatchManager() {
    if (wirelessManager) {
        wirelessManager->clearEspNowPacketHandler(PktType::kMatchCollection);
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
    activeMatchState.match.emplace(matchId, player ? player->getUserID().c_str() : "",
                                   localIsHunter);
    memcpy(activeMatchState.opponentMac.data(), opponentMac, 6);

    const UUIDBytes myPlayerUuid = player ? player->getServerPlayerId() : UUIDBytes{};
    const UUIDBytes myTeamUuid = player ? player->getRegistrationTeam().id : UUIDBytes{};
    const uint8_t* myMac = (wirelessManager && wirelessManager->getMacAddress())
                           ? wirelessManager->getMacAddress()
                           : nullptr;
    if (localIsHunter) {
        if (!myPlayerUuid.isZero()) {
            activeMatchState.match->setHunterPlayerId(myPlayerUuid);
        }
        activeMatchState.match->setHunterTeamId(myTeamUuid);
        activeMatchState.match->setHunterMac(myMac);
        activeMatchState.match->setBountyMac(opponentMac);
    } else {
        if (!myPlayerUuid.isZero()) {
            activeMatchState.match->setBountyPlayerId(myPlayerUuid);
        }
        activeMatchState.match->setBountyTeamId(myTeamUuid);
        activeMatchState.match->setBountyMac(myMac);
        activeMatchState.match->setHunterMac(opponentMac);
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
    if (opponentIsHunter) {
        activeMatchState.match->setHunterId(opponentId);
        if (!opponentPlayerUuid.isZero()) {
            activeMatchState.match->setHunterPlayerId(opponentPlayerUuid);
        }
        activeMatchState.match->setHunterTeamId(opponentTeamUuid);
    } else {
        activeMatchState.match->setBountyId(opponentId);
        if (!opponentPlayerUuid.isZero()) {
            activeMatchState.match->setBountyPlayerId(opponentPlayerUuid);
        }
        activeMatchState.match->setBountyTeamId(opponentTeamUuid);
    }

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

    const long hunterTime = activeMatchState.match->getHunterDrawTime();
    const long bountyTime = activeMatchState.match->getBountyDrawTime();
    return activeMatchState.localIsHunter ? hunterTime < bountyTime : bountyTime < hunterTime;
}

bool MatchManager::finalizeMatch() {
    if (!activeMatchState.match) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    // Snapshot times for the result screen (before clearCurrentMatch wipes the match).
    const long mySignedTime = activeMatchState.localIsHunter
                              ? activeMatchState.match->getHunterDrawTime()
                              : activeMatchState.match->getBountyDrawTime();
    const long opponentSignedTime = activeMatchState.localIsHunter
                                    ? activeMatchState.match->getBountyDrawTime()
                                    : activeMatchState.match->getHunterDrawTime();
    lastMatchDisplay.myTimeMs = static_cast<unsigned long>(std::max<long>(0, mySignedTime));
    lastMatchDisplay.opponentTimeMs =
    static_cast<unsigned long>(std::max<long>(0, opponentSignedTime));
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

    // Shootout matches are local-ephemeral: no save, no upload.
    if (currentMatchIsShootout()) {
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

bool MatchManager::setHunterDrawTime(long hunterTimeMs) {
    if (!activeMatchState.match) {
        return false;
    }
    activeMatchState.match->setHunterDrawTime(hunterTimeMs);
    return true;
}

bool MatchManager::setBountyDrawTime(long bountyTimeMs) {
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
    const uint32_t seq = getMatchSequenceNumber();
    uint32_t collected = storage->readUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0);
    if (collected > seq) {
        collected = seq;
        storage->writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, collected);
    }
    return collected;
}

void MatchManager::setCollectedHighWaterMark(uint32_t seq) {
    if (!storage) return;
    const uint32_t currentSeq = getMatchSequenceNumber();
    const uint32_t clamped = std::min(seq, currentSeq);
    const uint32_t currentCollected = getCollectedHighWaterMark();
    if (clamped > currentCollected) {
        storage->writeUInt(MATCHES_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, clamped);
    }
}

size_t MatchManager::getStoredMatchCount() {
    return std::min<size_t>(getMatchSequenceNumber(), MATCH_RING_CAPACITY);
}

size_t MatchManager::getUncollectedMatchCount() {
    const uint32_t seq = getMatchSequenceNumber();
    const uint32_t collected = getCollectedHighWaterMark();
    if (collected >= seq) return 0;
    return std::min<size_t>(seq - collected, MATCH_RING_CAPACITY);
}

bool MatchManager::appendMatchToStorage(const Match* match) {
    if (!match || !storage) return false;

    const uint32_t nextSeq = getMatchSequenceNumber() + 1;
    const uint8_t slot = static_cast<uint8_t>((nextSeq - 1) % MATCH_RING_CAPACITY);

    char key[16];
    snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX, slot);

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

bool MatchManager::readMatchFromSlot(uint8_t slot, Match& out) {
    if (!storage || slot >= MATCH_RING_CAPACITY) {
        return false;
    }

    char key[16];
    snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX, slot);

    uint8_t buffer[MATCH_BINARY_SIZE] = {};
    if (storage->readBytes(MATCHES_PREFS_NAMESPACE, key, buffer, MATCH_BINARY_SIZE) !=
        MATCH_BINARY_SIZE) {
        return false;
    }

    out.deserialize(buffer);
    return true;
}

bool MatchManager::readMatchBySequence(uint32_t seq, Match& out) {
    const uint32_t currentSeq = getMatchSequenceNumber();
    if (seq == 0 || seq > currentSeq) {
        return false;
    }
    if (currentSeq - seq >= MATCH_RING_CAPACITY) {
        return false;
    }
    const uint8_t slot = static_cast<uint8_t>((seq - 1) % MATCH_RING_CAPACITY);
    return readMatchFromSlot(slot, out);
}

const uint8_t* MatchManager::findConnectedFdnMac() const {
    if (!rdc) return nullptr;
    for (SerialIdentifier jack : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK,
                                  SerialIdentifier::INPUT_JACK_SECONDARY}) {
        if (rdc->getJackStatus(jack) == JackStatus::CONNECTED &&
            rdc->getPeerDeviceType(jack) == DeviceType::FDN) {
            const uint8_t* mac = rdc->getPeerMac(jack);
            if (mac) return mac;
        }
    }
    return nullptr;
}

bool MatchManager::isConnectedFdnMac(const uint8_t* mac) const {
    if (!rdc || !mac) return false;
    for (SerialIdentifier jack : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK,
                                  SerialIdentifier::INPUT_JACK_SECONDARY}) {
        if (rdc->getJackStatus(jack) == JackStatus::CONNECTED &&
            rdc->getPeerDeviceType(jack) == DeviceType::FDN) {
            const uint8_t* peer = rdc->getPeerMac(jack);
            if (peer && memcmp(peer, mac, 6) == 0) return true;
        }
    }
    return false;
}

void MatchManager::syncFdnMatchTransfer() {
    if (!storage || !wirelessManager) return;

    const uint8_t* fdnMac = findConnectedFdnMac();
    if (!fdnMac) {
        inFlightTransferSeq = 0;
        transferRetryTimer.invalidate();
        return;
    }

    const uint32_t seq = getMatchSequenceNumber();
    const uint32_t collected = getCollectedHighWaterMark();
    if (collected >= seq) {
        inFlightTransferSeq = 0;
        transferRetryTimer.invalidate();
        return;
    }

    const uint32_t oldestSeq = (seq > MATCH_RING_CAPACITY) ? (seq - MATCH_RING_CAPACITY + 1) : 1;
    const uint32_t nextToSend = std::max<uint32_t>(collected + 1, oldestSeq);

    if (inFlightTransferSeq == nextToSend && transferRetryTimer.isRunning() &&
        !transferRetryTimer.expired()) {
        return;
    }

    Match match{};
    if (!readMatchBySequence(nextToSend, match)) {
        return;
    }

    MatchTransferPacket pkt{};
    pkt.cmd = static_cast<uint8_t>(MatchCollectionCmd::RECORD);
    pkt.matchSeq = nextToSend;
    match.serialize(pkt.matchData);

    wirelessManager->sendEspNowData(fdnMac, PktType::kMatchCollection,
                                    reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    inFlightTransferSeq = nextToSend;
    transferRetryTimer.setTimer(MATCH_TRANSFER_RETRY_MS);
}

void MatchManager::onMatchCollectionPacket(const uint8_t* fromMac, const uint8_t* data,
                                           size_t dataLen) {
    if (!isConnectedFdnMac(fromMac)) {
        return;
    }
    if (!data || dataLen < sizeof(MatchTransferAckPacket)) {
        return;
    }
    if (data[0] != static_cast<uint8_t>(MatchCollectionCmd::ACK)) {
        return;
    }

    MatchTransferAckPacket ack{};
    memcpy(&ack, data, sizeof(ack));

    const uint32_t seq = getMatchSequenceNumber();
    const uint32_t collected = getCollectedHighWaterMark();
    const uint32_t oldestSeq = (seq > MATCH_RING_CAPACITY) ? (seq - MATCH_RING_CAPACITY + 1) : 1;

    if (ack.matchSeq >= oldestSeq && ack.matchSeq <= seq && ack.matchSeq > collected) {
        setCollectedHighWaterMark(ack.matchSeq);
        if (inFlightTransferSeq <= ack.matchSeq) {
            inFlightTransferSeq = 0;
            transferRetryTimer.invalidate();
        }
    }
}

parameterizedCallbackFunction MatchManager::getButtonMasher() {
    return buttonMasher;
}

void MatchManager::initialize(Player* player, StorageInterface* storage) {
    this->player = player;
    this->storage = storage;

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
        long boostedTimeMs = static_cast<long>(reactionTimeMs) - static_cast<long>(boost);
        if (boostedTimeMs < MIN_DRAW_TIME_MS) {
            boostedTimeMs = MIN_DRAW_TIME_MS;
        }

        // Snapshot boost at press time so the result screen can display it
        // even if supporters unplug before the match finalizes.
        matchManager->lastMatchDisplay.boostMs = boost;

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %ld ms (boost %lu) for %s",
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
        if (player && player->hasRegistrationProfile()) {
            if (command.playerUuid.isZero() || command.playerUuid == player->getServerPlayerId()) {
                LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: missing or self playerUuid");
                return;
            }
        } else {
            if (!command.playerUuid.isZero() && player &&
                command.playerUuid == player->getServerPlayerId()) {
                LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: self playerUuid");
                return;
            }
            if (command.playerId[0] == '\0' && command.playerUuid.isZero()) {
                LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: missing playerId");
                return;
            }
            if (player && !player->getUserID().empty() &&
                strcmp(command.playerId, player->getUserID().c_str()) == 0 &&
                command.playerUuid == player->getServerPlayerId()) {
                LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: self playerId");
                return;
            }
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
        if (activeMatchState.localIsHunter) {
            activeMatchState.match->setBountyId(command.playerId);
            if (!command.playerUuid.isZero()) {
                activeMatchState.match->setBountyPlayerId(command.playerUuid);
            }
            activeMatchState.match->setBountyTeamId(command.teamUuid);
        } else {
            activeMatchState.match->setHunterId(command.playerId);
            if (!command.playerUuid.isZero()) {
                activeMatchState.match->setHunterPlayerId(command.playerUuid);
            }
            activeMatchState.match->setHunterTeamId(command.teamUuid);
        }
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