#include "game/match-manager.hpp"
#include <ArduinoJson.h>
#include "device/drivers/logger.hpp"
#include "wireless/match-packet.hpp"
#include "game/shootout-manager.hpp"
#include "id-generator.hpp"
#include <optional>
#include <cstring>

static constexpr const char* PREF_COUNT_KEY = "count";
static constexpr const char* PREF_MATCH_KEY  = "match_";
static constexpr uint8_t     MAX_MATCHES      = 255;

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

MatchManager::MatchManager(WirelessManager* wirelessManager)
    : player(nullptr)
    , storage(nullptr)
    , resender(wirelessManager)
    , matchChannel(wirelessManager, &resender, PktType::kMatchCommand, nullptr) {
    matchChannel.onReceive([this](const uint8_t* fromMac, const MatchPacket& p) {
        MatchCommand command(fromMac, p.command, p.matchId, p.playerId, p.playerDrawTime,
                             p.isHunter);
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
    resender.setAbandonCallback(
    [this](PktType, uint8_t seqId, const uint8_t*, const uint8_t*, size_t) {
        if (activeMatchState.pendingAckSeqId == 0 || seqId != activeMatchState.pendingAckSeqId) {
            return;
        }
        clearCurrentMatch();
    });
}

void MatchManager::sync() {
    resender.sync();
}

uint8_t MatchManager::sendCommand(const uint8_t* mac, int command, const char* matchId,
                                  long drawTime, bool isHunter) {
    MatchPacket packet{};
    packet.command = command;
    packet.playerDrawTime = drawTime;
    packet.isHunter = isHunter;
    IdGenerator::copyId(packet.matchId, matchId);
    IdGenerator::copyId(packet.playerId, player->getUserID().c_str());

    LOG_I(MATCH_MANAGER_TAG, "Sending command %i to %s", command, MacToString(mac));
    return matchChannel.sendReliable(mac, packet);
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
                                uint8_t* opponentMac) {
    if (activeMatchState.match.has_value()) {
        return;
    }

    primeMatch(matchId, opponentMac, player->isHunter());
    if (opponentIsHunter) {
        activeMatchState.match->setHunterId(opponentId);
    } else {
        activeMatchState.match->setBountyId(opponentId);
    }

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING receiveMatch-ackSent T=%lu",
          clock ? clock->milliseconds() : 0UL);

    activeMatchState.pendingAckSeqId =
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

    // The whole result-screen snapshot is taken here, before clearCurrentMatch wipes the match.
    lastMatchDisplay.myTimeMs = activeMatchState.localIsHunter
                                ? activeMatchState.match->getHunterDrawTime()
                                : activeMatchState.match->getBountyDrawTime();
    lastMatchDisplay.opponentTimeMs = activeMatchState.localIsHunter
                                      ? activeMatchState.match->getBountyDrawTime()
                                      : activeMatchState.match->getHunterDrawTime();
    lastMatchDisplay.boostMs = activeMatchState.boostMs;
    lastMatchDisplay.penaltyMs = activeMatchState.penaltyMs;
    lastMatchDisplay.hasData = true;

    std::string matchId = activeMatchState.match->getMatchId();

    // Shootout matches are local-ephemeral: no save, no upload.
    if (currentMatchIsShootout()) {
        clearCurrentMatch();
        return true;
    }

    if (appendMatchToStorage(&*activeMatchState.match)) {
        updateStoredMatchCount(getStoredMatchCount() + 1);
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

std::string MatchManager::toJson() {
    // Create JSON document with an object at the root
    JsonDocument doc;  // Adjust size based on max matches
    
    // Create a "matches" array within the root object
    JsonArray matchArray = doc["matches"].to<JsonArray>();

    // Read all matches from storage
    uint8_t count = getStoredMatchCount();
    for (uint8_t i = 0; i < count; i++) {
        Match* match = readMatchFromStorage(i);
        if (match) {
            // Add match to array
            std::string matchJson = match->toJson();
            JsonDocument matchDoc;
            deserializeJson(matchDoc, matchJson);
            matchArray.add(matchDoc.as<JsonObject>());
            delete match;
        }
    }

    // Serialize to string
    std::string output;
    serializeJson(doc, output);
    return output;
}

void MatchManager::clearStorage() {
    storage->clear(MATCHES_PREFS_NAMESPACE);
    updateStoredMatchCount(0);
    LOG_I("PDN", "Cleared match storage\n");
}

size_t MatchManager::getStoredMatchCount() {
    return storage->readUChar(MATCHES_PREFS_NAMESPACE, PREF_COUNT_KEY, 0);
}

bool MatchManager::appendMatchToStorage(const Match* match) {
    if (!match) return false;

    uint8_t count = getStoredMatchCount();
    if (count >= MAX_MATCHES) {
        LOG_E("PDN", "Cannot save match - storage full\n");
        return false;
    }

    // Convert match to JSON for storage
    std::string matchJson = match->toJson();
    
    // Create key for this match
    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, count);

    LOG_W(MATCH_MANAGER_TAG, "Attempting to save match to storage at key %s (JSON length: %d bytes)", 
            key, matchJson.length());
    LOG_W(MATCH_MANAGER_TAG, "Match JSON: %s", matchJson.c_str());
    
    // Try to check if preferences is working
    if (storage->writeUChar(MATCHES_PREFS_NAMESPACE, "test_key", 123) != 1) {
        LOG_E(MATCH_MANAGER_TAG, "NVS Preference test write failed! Potential hardware or NVS issue");
    } else {
        uint8_t testValue = storage->readUChar(MATCHES_PREFS_NAMESPACE, "test_key", 0);
        LOG_W(MATCH_MANAGER_TAG, "NVS test write/read successful: wrote 123, read %d", testValue);
    }

    // Save match JSON to preferences
    if (storage->write(MATCHES_PREFS_NAMESPACE, key, matchJson) != matchJson.length()) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to save match to storage - key: %s, length: %d", 
                key, matchJson.length());
        
        return false;
    }

    LOG_W(MATCH_MANAGER_TAG, "Successfully saved match to storage at index %d", count);
    return true;
}

void MatchManager::updateStoredMatchCount(uint8_t count) {
    if (storage->writeUChar(MATCHES_PREFS_NAMESPACE, PREF_COUNT_KEY, count) != 1) {
        LOG_E("PDN", "Failed to update match count\n");
    } else {
        LOG_I("PDN", "Updated stored match count to %d\n", count);
    }
}

Match* MatchManager::readMatchFromStorage(uint8_t index) {
    if (index >= getStoredMatchCount()) {
        return nullptr;
    }

    // Create key for this match
    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, index);
    
    // Read match JSON from preferences
    std::string matchJson = storage->read(MATCHES_PREFS_NAMESPACE, key, "");
    if (matchJson.empty()) {
        return nullptr;
    }

    // Create and deserialize match
    Match* match = new Match();
    match->fromJson(matchJson);
    return match;
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

        const unsigned long penaltyMs = activeMatchState->calculateButtonMasherPenalty();
        reactionTimeMs += penaltyMs;

        unsigned long boost = matchManager->boostProvider_ ? matchManager->boostProvider_() : 0;
        unsigned long boostedTimeMs = (boost >= reactionTimeMs) ? 0 : (reactionTimeMs - boost);

        // Snapshot boost at press time so the result screen can display it
        // even if supporters unplug before the match finalizes.
        activeMatchState->boostMs = boost;
        activeMatchState->penaltyMs = penaltyMs;

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
        if (!rdc_ || !rdc_->isDirectPeer(command.wifiMacAddr)) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: sender not an RDC direct peer");
            return;
        }
        bool shootoutActive = shootoutManager_ && shootoutManager_->active();
        if(!shootoutActive && player->isHunter() == command.isHunter) {
            // Echo the sender's matchId/MAC: activeMatchState.match is still empty.
            sendCommand(command.wifiMacAddr, MatchCmd::MATCH_ROLE_MISMATCH, command.matchId, 0,
                        player->isHunter());
            return;
        } else {
            receiveMatch(command.matchId, command.playerId, command.isHunter, const_cast<uint8_t*>(command.wifiMacAddr));
        }
    } else if (command.command == MatchCmd::MATCH_ID_ACK) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ID_ACK command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        if (activeMatchState.matchIsReady || activeMatchState.pendingAckSeqId != 0) return;
        matchChannel.cancel(activeMatchState.opponentMac.data());
        if (activeMatchState.localIsHunter) {
            activeMatchState.match->setBountyId(command.playerId);
        } else {
            activeMatchState.match->setHunterId(command.playerId);
        }
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