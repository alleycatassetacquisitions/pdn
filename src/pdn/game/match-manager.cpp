#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/quickdraw-packet.hpp"
#include "game/shootout-manager.hpp"
#include "id-generator.hpp"
#include <optional>
#include <cstring>

static constexpr const char* PREF_COUNT_KEY = "count";
static constexpr const char* PREF_MATCH_KEY  = "match_";
static constexpr const char* PREF_SLOT_KEY = "slot";
// Ring size, not a ceiling: the oldest record is overwritten so a save never
// fails. Far below the 255 an index byte allows, because the 20KB `nvs`
// partition is shared with the crash log, whose writes report nothing when they
// fail. A stored match is ~145 bytes of JSON; its on-flash cost is larger and
// wants measuring, so this is the conservative side of it.
static constexpr uint8_t MAX_MATCHES = 32;

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

MatchManager::MatchManager(WirelessManager* wirelessManager)
    : player(nullptr)
    , storage(nullptr)
    , resender(wirelessManager)
    , duelChannel(wirelessManager, &resender, PktType::kQuickdrawCommand, nullptr) {
    duelChannel.onReceive([this](const uint8_t* fromMac, const QuickdrawPacket& p) {
        QuickdrawCommand command(fromMac, p.command, p.matchId, p.playerId,
                                 p.playerDrawTime, p.isHunter);
        listenForMatchEvents(command);
    });
}

void MatchManager::sync() {
    resender.sync();
}

void MatchManager::sendCommand(const uint8_t* mac, int command, const char* matchId,
                               long drawTime, bool isHunter) {
    QuickdrawPacket packet{};
    packet.command = command;
    packet.playerDrawTime = drawTime;
    packet.isHunter = isHunter;
    IdGenerator::copyId(packet.matchId, matchId);
    IdGenerator::copyId(packet.playerId, player->getUserID().c_str());

    LOG_I(MATCH_MANAGER_TAG, "Sending command %i to %s", command, MacToString(mac));
    duelChannel.sendReliable(mac, packet);
}

MatchManager::~MatchManager() {
    if (storage) {
        storage->end();
    }
}

void MatchManager::setBoostProvider(std::function<unsigned long()> provider) {
    boostProvider_ = provider;
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
    return activeDuelState.match.has_value() &&
           strncmp(activeDuelState.match->getMatchId(), kShootoutMatchIdPrefix,
                   sizeof(kShootoutMatchIdPrefix) - 1) == 0;
}

void MatchManager::clearShootoutMatch() {
    if (!currentMatchIsShootout()) return;
    clearCurrentMatch();
}

void MatchManager::clearCurrentMatch() {
    if (!activeDuelState.match) return;
    LOG_I(MATCH_MANAGER_TAG, "Clearing current match");
    // Keep duelChannel retrying only when a local DRAW_RESULT or NEVER_PRESSED was sent.
    if (!activeDuelState.hasPressedButton && !activeDuelState.gracePeriodExpiredNoResult) {
        duelChannel.cancel(activeDuelState.opponentMac.data());
    }
    activeDuelState = ActiveDuelState{};
}

void MatchManager::primeMatch(const char* matchId, const uint8_t* opponentMac,
                              bool localIsHunter) {
    activeDuelState.localIsHunter = localIsHunter;
    activeDuelState.match.emplace(matchId, player->getUserID().c_str(), localIsHunter);
    memcpy(activeDuelState.opponentMac.data(), opponentMac, 6);
}

void MatchManager::initializeMatch(uint8_t* opponentMac) {
    if (activeDuelState.match.has_value()) {
        return;
    }

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING initializeMatch T=%lu", clock ? clock->milliseconds() : 0UL);

    primeMatch(IdGenerator::getInstance().generateId(), opponentMac, player->isHunter());
    sendCommand(activeDuelState.opponentMac.data(), QDCommand::SEND_MATCH_ID,
                activeDuelState.match->getMatchId(), 0, activeDuelState.localIsHunter);
}

void MatchManager::initializeShootoutMatch(const char* matchId, uint8_t* opponentMac,
                                           bool localIsHunter) {
    // A repeat MATCH_START names a bout that may already be mounted; the live one
    // wins, since re-priming would reset a draw already in progress.
    if (activeDuelState.match.has_value()) return;

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING initializeShootoutMatch T=%lu",
          clock ? clock->milliseconds() : 0UL);

    primeMatch(matchId, opponentMac, localIsHunter);
    activeDuelState.matchIsReady = true;
}

void MatchManager::receiveMatch(const char* matchId, const char* opponentId, bool opponentIsHunter, uint8_t* opponentMac) {
    // Only allow one active match at a time
    if (activeDuelState.match.has_value()) {
        return;
    }

    // Build the match from our own perspective, then record the opponent's ID.
    primeMatch(matchId, opponentMac, player->isHunter());
    if (opponentIsHunter) {
        activeDuelState.match->setHunterId(opponentId);
    } else {
        activeDuelState.match->setBountyId(opponentId);
    }

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING receiveMatch-ready T=%lu", clock ? clock->milliseconds() : 0UL);

    activeDuelState.matchIsReady = true;

    sendCommand(activeDuelState.opponentMac.data(), QDCommand::MATCH_ID_ACK,
                activeDuelState.match->getMatchId(), 0, activeDuelState.localIsHunter);
}


void MatchManager::setDuelLocalStartTime(unsigned long local_start_time_ms) {
    activeDuelState.duelLocalStartTime = local_start_time_ms;
}

unsigned long MatchManager::getDuelLocalStartTime() {
    return activeDuelState.duelLocalStartTime;
}

// //Pretty sure this needs to be refactored. will come back to it.
bool MatchManager::matchResultsAreIn() {
    if (!activeDuelState.match) {
        return false;
    }

    // Final clause is the deadlock escape for both-timed-out duels where the
    // opponent's NEVER_PRESSED packet drops. Gated on !hasPressedButton so a
    // button-press in the same tick as grace expiry still takes the press
    // path (clauses 1/2) — otherwise didWin() would spuriously return false.
    return (activeDuelState.hasReceivedDrawResult && activeDuelState.hasPressedButton)
    || (activeDuelState.opponentNeverPressed && activeDuelState.hasPressedButton)
    || (activeDuelState.gracePeriodExpiredNoResult && !activeDuelState.hasPressedButton);
}

void MatchManager::setNeverPressed() {
    activeDuelState.gracePeriodExpiredNoResult = true;
}

bool MatchManager::didWin() {
    if (!activeDuelState.match) {
        return false;
    }

    if (activeDuelState.opponentNeverPressed) {
        return true;
    }
    if (activeDuelState.gracePeriodExpiredNoResult) {
        return false;
    }

    const unsigned long hunterTime = activeDuelState.match->getHunterDrawTime();
    const unsigned long bountyTime = activeDuelState.match->getBountyDrawTime();
    return activeDuelState.localIsHunter ? hunterTime < bountyTime : bountyTime < hunterTime;
}

bool MatchManager::finalizeMatch() {
    if (!activeDuelState.match) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    // Snapshot times for the result screen (before clearCurrentMatch wipes the match).
    lastMatchDisplay.myTimeMs = activeDuelState.localIsHunter
                                    ? activeDuelState.match->getHunterDrawTime()
                                    : activeDuelState.match->getBountyDrawTime();
    lastMatchDisplay.opponentTimeMs = activeDuelState.localIsHunter
                                          ? activeDuelState.match->getBountyDrawTime()
                                          : activeDuelState.match->getHunterDrawTime();
    lastMatchDisplay.hasData = true;

    std::string match_id = activeDuelState.match->getMatchId();

    // Shootout matches are local-ephemeral: never written to storage.
    if (currentMatchIsShootout()) {
        clearCurrentMatch();
        return true;
    }

    // Save to storage
    if (appendMatchToStorage(&*activeDuelState.match)) {
        clearCurrentMatch();
        LOG_I(MATCH_MANAGER_TAG, "Successfully finalized match %s\n", match_id.c_str());
        return true;
    }

    LOG_E("PDN", "Failed to finalize match %s\n", match_id.c_str());
    return false;
}

bool MatchManager::getHasReceivedDrawResult() {
    return activeDuelState.hasReceivedDrawResult;
}

bool MatchManager::getHasPressedButton() {
    return activeDuelState.hasPressedButton;
}

void MatchManager::setReceivedDrawResult() {
    activeDuelState.hasReceivedDrawResult = true;
}

parameterizedCallbackFunction MatchManager::getDuelButtonPush() {
    return duelButtonPush;
}

void MatchManager::setReceivedButtonPush() {
    activeDuelState.hasPressedButton = true;
}

bool MatchManager::setHunterDrawTime(unsigned long hunter_time_ms) {
    if (!activeDuelState.match) {
        return false;
    }
    activeDuelState.match->setHunterDrawTime(hunter_time_ms);
    return true;
}

bool MatchManager::setBountyDrawTime(unsigned long bounty_time_ms) {
    if (!activeDuelState.match) {
        return false;
    }
    activeDuelState.match->setBountyDrawTime(bounty_time_ms);
    return true;
}

Match* MatchManager::readMatchFromStorage(uint8_t index) {
    const uint8_t retained = static_cast<uint8_t>(getStoredMatchCount());
    if (index >= retained) {
        return nullptr;
    }

    // Oldest first. Until the ring wraps the slots are already in order; once it
    // has, the next slot due to be written is the oldest one still held.
    const uint8_t oldest = (retained == MAX_MATCHES) ? nextStorageSlot() : 0;
    const uint8_t slot = static_cast<uint8_t>((oldest + index) % MAX_MATCHES);

    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, slot);

    std::string matchJson = storage->read(MATCHES_PREFS_NAMESPACE, key, "");
    if (matchJson.empty()) {
        return nullptr;
    }

    Match* match = new Match();
    match->fromJson(matchJson);
    return match;
}

void MatchManager::clearStorage() {
    storage->clear(MATCHES_PREFS_NAMESPACE);
    updateStoredMatchCount(0);
    setNextStorageSlot(0);
    LOG_I("PDN", "Cleared match storage\n");
}

uint8_t MatchManager::nextStorageSlot() {
    return storage->readUChar(MATCHES_PREFS_NAMESPACE, PREF_SLOT_KEY, 0);
}

void MatchManager::setNextStorageSlot(uint8_t slot) {
    if (storage->writeUChar(MATCHES_PREFS_NAMESPACE, PREF_SLOT_KEY, slot) != 1) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to advance the match storage slot");
    }
}

size_t MatchManager::getStoredMatchCount() {
    return storage->readUChar(MATCHES_PREFS_NAMESPACE, PREF_COUNT_KEY, 0);
}

bool MatchManager::appendMatchToStorage(const Match* match) {
    if (!match) return false;

    const uint8_t slot = nextStorageSlot();
    const std::string matchJson = match->toJson();

    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, slot);

    if (storage->write(MATCHES_PREFS_NAMESPACE, key, matchJson) != matchJson.length()) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to save match to storage - key: %s, length: %d", key,
              matchJson.length());
        return false;
    }

    setNextStorageSlot(static_cast<uint8_t>((slot + 1) % MAX_MATCHES));

    const uint8_t retained = static_cast<uint8_t>(getStoredMatchCount());
    if (retained < MAX_MATCHES) {
        updateStoredMatchCount(static_cast<uint8_t>(retained + 1));
    }
    return true;
}

void MatchManager::updateStoredMatchCount(uint8_t count) {
    if (storage->writeUChar(MATCHES_PREFS_NAMESPACE, PREF_COUNT_KEY, count) != 1) {
        LOG_E("PDN", "Failed to update match count\n");
    } else {
        LOG_I("PDN", "Updated stored match count to %d\n", count);
    }
}

parameterizedCallbackFunction MatchManager::getButtonMasher() {
    return buttonMasher;
}

void MatchManager::initialize(Player* player, StorageInterface* storage) {
    this->player = player;
    this->storage = storage;

    duelButtonPush = [](void *ctx) {
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
        ActiveDuelState* activeDuelState = &matchManager->activeDuelState;
        Player* player = matchManager->player;

        if(matchManager->getHasPressedButton()) {
            LOG_I(MATCH_MANAGER_TAG, "Button already pressed - skipping");
            return;
        }

        if(!matchManager->getCurrentMatch().has_value()) {
            LOG_E(MATCH_MANAGER_TAG, "No current match - skipping");
            return;
        }

        unsigned long reactionTimeMs = now - matchManager->getDuelLocalStartTime();

        if(activeDuelState->buttonMasherCount > 0) {
            reactionTimeMs = reactionTimeMs + activeDuelState->calculateButtonMasherPenalty();
        }

        unsigned long boost = matchManager->boostProvider_ ? matchManager->boostProvider_() : 0;
        unsigned long boostedTimeMs = (boost >= reactionTimeMs) ? 0 : (reactionTimeMs - boost);

        // Snapshot boost at press time so the result screen can display it
        // even if supporters unplug before the match finalizes.
        matchManager->lastMatchDisplay.boostMs = boost;

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms (boost %lu) for %s",
              boostedTimeMs, boost, activeDuelState->localIsHunter ? "Hunter" : "Bounty");

        activeDuelState->localIsHunter ? matchManager->setHunterDrawTime(boostedTimeMs)
                                       : matchManager->setBountyDrawTime(boostedTimeMs);

        // Player stats record the raw (unboosted) reaction time — boost is a
        // duel-time advantage, not an achievement the player actually made.
        player->addReactionTime(reactionTimeMs);

        LOG_I(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        LOG_I(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s",
                MacToString(activeDuelState->opponentMac.data()));

        // Send the BOOSTED time in DRAW_RESULT so both sides agree on who won.
        matchManager->sendCommand(activeDuelState->opponentMac.data(), QDCommand::DRAW_RESULT,
                                  matchManager->getCurrentMatch()->getMatchId(),
                                  boostedTimeMs, activeDuelState->localIsHunter);

        matchManager->setReceivedButtonPush();
        
        LOG_I(MATCH_MANAGER_TAG, "Reaction time: %lu ms", reactionTimeMs);
    };

    buttonMasher = [](void *ctx) {
        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        matchManager->activeDuelState.buttonMasherCount++;
        LOG_I(MATCH_MANAGER_TAG, "Button masher count: %d", matchManager->activeDuelState.buttonMasherCount);
    };
}

bool MatchManager::isFromActiveMatchOpponent(const QuickdrawCommand& command) const {
    if (!activeDuelState.match.has_value()) return false;
    if (memcmp(command.wifiMacAddr, activeDuelState.opponentMac.data(), 6) != 0) return false;
    if (strcmp(command.matchId, activeDuelState.match->getMatchId()) != 0) return false;
    return true;
}

void MatchManager::listenForMatchEvents(const QuickdrawCommand& command) {
    LOG_I(MATCH_MANAGER_TAG, "Received command: %d", command.command);

    if(command.command == QDCommand::SEND_MATCH_ID) {
        // Gate on RDC peering — no active match exists yet, so the sender
        // must be authenticated via the cable-established direct peer.
        LOG_I(MATCH_MANAGER_TAG, "Received SEND_MATCH_ID command from opponent");
        if (!rdc_ || !rdc_->isDirectPeer(command.wifiMacAddr)) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: sender not an RDC direct peer");
            return;
        }
        bool shootoutActive = shootoutManager_ && shootoutManager_->active();
        if(!shootoutActive && player->isHunter() == command.isHunter) {
            // Echo the sender's matchId/MAC: activeDuelState.match is still empty.
            sendCommand(command.wifiMacAddr, QDCommand::MATCH_ROLE_MISMATCH,
                        command.matchId, 0, player->isHunter());
            return;
        } else {
            receiveMatch(command.matchId, command.playerId, command.isHunter, const_cast<uint8_t*>(command.wifiMacAddr));
        }
    } else if(command.command == QDCommand::MATCH_ID_ACK) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ID_ACK command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        duelChannel.cancel(activeDuelState.opponentMac.data());
        if (activeDuelState.localIsHunter) {
            activeDuelState.match->setBountyId(command.playerId);
        } else {
            activeDuelState.match->setHunterId(command.playerId);
        }
        auto* clock = SimpleTimer::getPlatformClock();
        LOG_D(MATCH_MANAGER_TAG, "TIMING matchAck-ready T=%lu", clock ? clock->milliseconds() : 0UL);
        activeDuelState.matchIsReady = true;
    } else if(command.command == QDCommand::MATCH_ROLE_MISMATCH) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ROLE_MISMATCH command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        clearCurrentMatch();
        return;
    } else if(command.command == QDCommand::DRAW_RESULT || command.command == QDCommand::NEVER_PRESSED) {
        LOG_I(MATCH_MANAGER_TAG, "Received DRAW_RESULT command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;

        long opponentTime = command.playerDrawTime;

        LOG_I(MATCH_MANAGER_TAG, "Opponent reaction time: %ld ms", opponentTime);

        command.isHunter ?
        setHunterDrawTime(opponentTime)
        : setBountyDrawTime(opponentTime);

        if (command.command == QDCommand::NEVER_PRESSED) {
            activeDuelState.opponentNeverPressed = true;
        }

        setReceivedDrawResult();
    } else {
        LOG_W(MATCH_MANAGER_TAG, "Received unexpected command in Match Manager: %d", command.command);
    }
}

void MatchManager::sendNeverPressed(unsigned long pityTime) {
    if (!activeDuelState.match.has_value()) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot send NEVER_PRESSED - no active match");
        return;
    }

    activeDuelState.localIsHunter ? setHunterDrawTime(pityTime) : setBountyDrawTime(pityTime);
    setNeverPressed();
    // Pity times are synthetic but still count toward the average.
    player->addReactionTime(pityTime);

    sendCommand(activeDuelState.opponentMac.data(), QDCommand::NEVER_PRESSED,
                activeDuelState.match->getMatchId(), pityTime, activeDuelState.localIsHunter);
}

bool MatchManager::isMatchReady() {
    return activeDuelState.matchIsReady;
}