#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/match-packet.hpp"
#include "game/posse-manager.hpp"
#include "game/shootout-manager.hpp"
#include "id-generator.hpp"
#include <optional>
#include <algorithm>
#include <cstring>

static constexpr const char* PREF_SEQ_KEY = "seq";
static constexpr const char* PREF_COLLECTED_SEQ_KEY = "collectedSeq";
static constexpr const char* PREF_MATCH_KEY = "match_";

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

MatchManager::MatchManager(WirelessManager* wirelessManager)
    : player(nullptr)
    , storage(nullptr)
    , resender(wirelessManager)
    , matchChannel(wirelessManager, &resender, PktType::kMatchCommand, nullptr) {
    matchChannel.onReceive([this](const uint8_t* fromMac, const MatchPacket& p) {
        MatchCommand command(fromMac, p.command, p.matchId, p.playerDrawTime, p.isHunter,
                             p.playerUuid, p.teamUuid);
        command.boostMs = p.boostMs;
        listenForMatchEvents(command);
    });
    // Ready only on SEND_SUCCESS for the ack: the hunter's radio has then taken it.
    matchChannel.setOnDelivered([this](uint8_t seqId, const uint8_t*) {
        if (seqId != activeMatchState.pendingAckSeqId) {
            return;
        }
        activeMatchState.pendingAckSeqId = 0;
        activeMatchState.matchIsReady = true;
    });
    // No delivery report within the retransmit span: drop the match rather than
    // count down against a hunter that may never have heard the ack.
    resender.setAbandonCallback(
    [this](PktType, uint8_t seqId, const uint8_t*, const uint8_t*, size_t) {
        if (seqId != activeMatchState.pendingAckSeqId) {
            return;
        }
        clearCurrentMatch();
    });
}

void MatchManager::sync() {
    resender.sync();
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
    if (storage) {
        storage->end();
    }
}

void MatchManager::setBoostProvider(std::function<unsigned long()> provider) {
    boostProvider = std::move(provider);
}

void MatchManager::setRemoteDeviceCoordinator(RemoteDeviceCoordinator* rdc) {
    rdc_ = rdc;
}

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
    activeMatchState.match.emplace(matchId);
    activeMatchState.match->setPlayer(localIsHunter, player->getServerPlayerId(),
                                      player->getRegistrationTeam().id);
    memcpy(activeMatchState.opponentMac.data(), opponentMac, 6);
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

void MatchManager::receiveMatch(const char* matchId, const UUIDBytes& opponentPlayerId,
                                const UUIDBytes& opponentTeamId, bool opponentIsHunter,
                                uint8_t* opponentMac) {
    if (activeMatchState.match.has_value()) {
        return;
    }

    primeMatch(matchId, opponentMac, player->isHunter());
    activeMatchState.match->setPlayer(opponentIsHunter, opponentPlayerId, opponentTeamId);

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
    // An exact tie is a loss on both screens; the record still needs one winner.
    if (activeMatchState.hasReceivedDrawResult && !activeMatchState.opponentNeverPressed &&
        getEffectiveDrawTime(true) == getEffectiveDrawTime(false)) {
        return false;
    }
    return isHunterWinner() == activeMatchState.localIsHunter;
}

bool MatchManager::isHunterWinner() const {
    const bool localIsHunter = activeMatchState.localIsHunter;
    if (activeMatchState.opponentNeverPressed) {
        return localIsHunter;
    }
    // A boosted time below zero must not beat a result that never arrived.
    if (activeMatchState.gracePeriodExpiredNoResult || !activeMatchState.hasReceivedDrawResult) {
        return !localIsHunter;
    }
    // The record gives an exact tie to the Bounty.
    return getEffectiveDrawTime(true) < getEffectiveDrawTime(false);
}

bool MatchManager::isWinnerAgreed() const {
    const bool localNeverPressed = activeMatchState.gracePeriodExpiredNoResult;
    const bool opponentNeverPressed = activeMatchState.opponentNeverPressed;
    // A side that never pressed loses on both devices; when both never pressed,
    // each device names itself the winner.
    if (localNeverPressed != opponentNeverPressed) return true;
    if (localNeverPressed) return false;
    // Without the opponent's result this device names it the winner unseen,
    // while the opponent compares the two times.
    return activeMatchState.hasReceivedDrawResult;
}

bool MatchManager::finalizeMatch() {
    if (!activeMatchState.match) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    Match& match = *activeMatchState.match;
    const long hunterTime = getEffectiveDrawTime(true);
    const long bountyTime = getEffectiveDrawTime(false);
    // The record keeps raw times, so it is told the winner.
    match.setWinnerPlayerId(isHunterWinner() ? match.getHunterPlayerId()
                                             : match.getBountyPlayerId());
    if (PlatformClock* clock = SimpleTimer::getPlatformClock()) {
        match.setOccurredAtUnixMs(clock->unixMilliseconds());
    }

    // The whole result-screen snapshot is taken here, before clearCurrentMatch wipes the match.
    lastMatchDisplay.myTimeMs = activeMatchState.localIsHunter ? hunterTime : bountyTime;
    lastMatchDisplay.opponentTimeMs = activeMatchState.localIsHunter ? bountyTime : hunterTime;
    lastMatchDisplay.boostMs = activeMatchState.boostMs;
    lastMatchDisplay.penaltyMs = activeMatchState.penaltyMs;
    lastMatchDisplay.hasData = true;

    // Shootout matches are local-ephemeral. The server needs two different
    // registered players and a winner, so a record naming a nil or repeated
    // player, or a winner the opponent's record could contradict, is not kept.
    if (currentMatchIsShootout() || match.getHunterPlayerId().isZero() ||
        match.getBountyPlayerId().isZero() ||
        match.getHunterPlayerId() == match.getBountyPlayerId() || !isWinnerAgreed()) {
        clearCurrentMatch();
        return true;
    }

    const bool stored = appendRecordToStorage(match);
    if (stored) {
        LOG_I(MATCH_MANAGER_TAG, "Stored match %s", match.getMatchId());
    }
    clearCurrentMatch();
    return stored;
}

bool MatchManager::getHasReceivedDrawResult() const {
    return activeMatchState.hasReceivedDrawResult;
}

bool MatchManager::getHasPressedButton() const { return activeMatchState.hasPressedButton; }

void MatchManager::setReceivedDrawResult() { activeMatchState.hasReceivedDrawResult = true; }

parameterizedCallbackFunction MatchManager::getMatchButtonPush() { return matchButtonPush; }

void MatchManager::setReceivedButtonPush() { activeMatchState.hasPressedButton = true; }

bool MatchManager::setHunterDrawTime(unsigned long hunter_time_ms) {
    if (!activeMatchState.match) {
        return false;
    }
    activeMatchState.match->setHunterDrawTime(hunter_time_ms);
    return true;
}

bool MatchManager::setBountyDrawTime(unsigned long bounty_time_ms) {
    if (!activeMatchState.match) {
        return false;
    }
    activeMatchState.match->setBountyDrawTime(bounty_time_ms);
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

// The slot record sequenceNumber (from 1) occupies.
static void matchSlotKey(uint32_t sequenceNumber, char (&key)[16]) {
    snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY,
             static_cast<unsigned>((sequenceNumber - 1) % MatchManager::MATCH_RING_CAPACITY));
}

uint32_t MatchManager::getMatchSequenceNumber() {
    return storage->readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0);
}

uint32_t MatchManager::getCollectedHighWaterMark() {
    return std::min(storage->readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0),
                    getMatchSequenceNumber());
}

uint32_t MatchManager::getFirstPendingSequenceNumber() {
    const uint32_t newest = getMatchSequenceNumber();
    const uint32_t oldestInRing =
    newest > MATCH_RING_CAPACITY ? newest - MATCH_RING_CAPACITY + 1 : 1;
    const uint32_t first = std::max(getCollectedHighWaterMark() + 1, oldestInRing);
    return first <= newest ? first : 0;
}

bool MatchManager::readRecord(uint32_t sequenceNumber, MatchRecord& record) {
    const uint32_t newest = getMatchSequenceNumber();
    if (sequenceNumber == 0 || sequenceNumber > newest ||
        newest - sequenceNumber >= MATCH_RING_CAPACITY) {
        return false;
    }
    char key[16];
    matchSlotKey(sequenceNumber, key);
    return storage->readBytes(MATCH_RING_PREFS_NAMESPACE, key, reinterpret_cast<uint8_t*>(&record),
                              sizeof(MatchRecord)) == sizeof(MatchRecord);
}

bool MatchManager::markCollected(uint32_t sequenceNumber) {
    if (sequenceNumber <= getCollectedHighWaterMark() ||
        sequenceNumber > getMatchSequenceNumber()) {
        return false;
    }
    if (storage->writeUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, sequenceNumber) !=
        sizeof(uint32_t)) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to mark match %u collected", sequenceNumber);
        return false;
    }
    return true;
}

bool MatchManager::appendRecordToStorage(const Match& match) {
    const uint32_t newest = getMatchSequenceNumber();
    // Every slot holds a record no FDN has taken yet; overwriting one would lose it.
    if (newest - getCollectedHighWaterMark() >= MATCH_RING_CAPACITY) {
        LOG_E(MATCH_MANAGER_TAG, "Match %s not stored: %u matches wait for an FDN",
              match.getMatchId(), MATCH_RING_CAPACITY);
        return false;
    }

    const uint32_t nextSeq = newest + 1;
    char key[16];
    matchSlotKey(nextSeq, key);
    uint8_t record[sizeof(MatchRecord)];
    match.serialize(record);
    if (storage->writeBytes(MATCH_RING_PREFS_NAMESPACE, key, record, sizeof(record)) !=
        sizeof(record)) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to save match record at key %s", key);
        return false;
    }
    // "seq" moves last, so a failed write loses no pending record: the slot it
    // overwrote belonged to a collected one.
    if (storage->writeUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, nextSeq) != sizeof(uint32_t)) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to update match sequence number to %u", nextSeq);
        return false;
    }
    return true;
}

parameterizedCallbackFunction MatchManager::getButtonMasher() {
    return buttonMasher;
}

void MatchManager::clearButtonMasherCount() { activeMatchState.buttonMasherCount = 0; }

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

        const unsigned long penaltyMs = activeMatchState->calculateButtonMasherPenalty();
        reactionTimeMs += penaltyMs;

        // Same bound the opponent applies to the frame's boost.
        const unsigned long boost =
        std::min(matchManager->boostProvider ? matchManager->boostProvider() : 0UL,
                 PosseManager::getMaxBoostMs());

        // Snapshot boost at press time so the result screen can display it
        // even if supporters unplug before the match finalizes.
        activeMatchState->boostMs = boost;
        activeMatchState->penaltyMs = penaltyMs;

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms (boost %lu) for %s",
              reactionTimeMs, boost, activeMatchState->localIsHunter ? "Hunter" : "Bounty");

        activeMatchState->localIsHunter ? matchManager->setHunterDrawTime(reactionTimeMs)
                                        : matchManager->setBountyDrawTime(reactionTimeMs);

        // Player stats record the raw (unboosted) reaction time — boost is a
        // match-time advantage, not an achievement the player actually made.
        player->addReactionTime(reactionTimeMs);

        LOG_I(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        LOG_I(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s",
              MacToString(activeMatchState->opponentMac.data()));

        // Raw time for the opponent's record, boost for its comparison.
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
        // Gate on RDC peering — no active match exists yet, so the sender
        // must be authenticated via the cable-established direct peer.
        LOG_I(MATCH_MANAGER_TAG, "Received SEND_MATCH_ID command from opponent");
        if (!rdc_ || !rdc_->isDirectPeer(command.wifiMacAddr)) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: sender not an RDC direct peer");
            return;
        }
        bool shootoutActive = shootoutManager_ && shootoutManager_->active();
        if(!shootoutActive && player->isHunter() == command.isHunter) {
            // Echo the sender's matchId/MAC: activeMatchState.match is still empty.
            sendCommand(command.wifiMacAddr, MatchCmd::MATCH_ROLE_MISMATCH, command.matchId, 0, 0,
                        player->isHunter());
            return;
        } else {
            receiveMatch(command.matchId, command.playerUuid, command.teamUuid, command.isHunter,
                         const_cast<uint8_t*>(command.wifiMacAddr));
        }
    } else if (command.command == MatchCmd::MATCH_ID_ACK) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ID_ACK command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        if (activeMatchState.matchIsReady || activeMatchState.pendingAckSeqId != 0) return;
        matchChannel.cancel(activeMatchState.opponentMac.data());
        activeMatchState.match->setPlayer(!activeMatchState.localIsHunter, command.playerUuid,
                                          command.teamUuid);
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
        // A negative raw time would beat every honest draw.
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