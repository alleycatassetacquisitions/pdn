#include "game/match-manager.hpp"
#include <ArduinoJson.h>
#include "device/drivers/logger.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "device/device-constants.hpp"
#include "id-generator.hpp"
#include <optional>

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

MatchManager::MatchManager() 
    : player(nullptr)
    , storage(nullptr)
    , quickdrawWirelessManager(nullptr) {
}

MatchManager::~MatchManager() { 
    quickdrawWirelessManager = nullptr;
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

void MatchManager::clearCurrentMatch() {
    if (activeDuelState.match) {
        LOG_I(MATCH_MANAGER_TAG, "Clearing current match");
        activeDuelState.match.reset();
        activeDuelState.hasReceivedDrawResult = false;
        activeDuelState.hasPressedButton = false;
        activeDuelState.duelLocalStartTime = 0;
        activeDuelState.gracePeriodExpiredNoResult = false;
        activeDuelState.opponentNeverPressed = false;
        activeDuelState.buttonMasherCount = 0;
        activeDuelState.matchIsReady = false;
    }
}

void MatchManager::initializeMatch(uint8_t* opponentMac) {
    // Only allow one active match at a time
    if (activeDuelState.match.has_value()) {
        return;
    }

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_W(MATCH_MANAGER_TAG, "TIMING initializeMatch T=%lu", clock ? clock->milliseconds() : 0UL);

    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    memcpy(matchId, IdGenerator::getInstance().generateId(), IdGenerator::UUID_BUFFER_SIZE);
    activeDuelState.match.emplace(matchId, player->getUserID().c_str(), player->isHunter());
    memcpy(activeDuelState.opponentMac.data(), opponentMac, 6);
    sendMatchId();
}

void MatchManager::sendMatchId() {
    QuickdrawCommand command(activeDuelState.opponentMac.data(), QDCommand::SEND_MATCH_ID, activeDuelState.match->getMatchId(), player->getUserID().c_str(), 0, player->isHunter());
    quickdrawWirelessManager->broadcastPacket(activeDuelState.opponentMac.data(), command);
}

void MatchManager::receiveMatch(const char* matchId, const char* opponentId, bool opponentIsHunter, uint8_t* opponentMac) {
    // Only allow one active match at a time
    if (activeDuelState.match.has_value()) {
        return;
    }

    // Build the match from our own perspective, then record the opponent's ID.
    activeDuelState.match.emplace(matchId, player->getUserID().c_str(), player->isHunter());
    if (opponentIsHunter) {
        activeDuelState.match->setHunterId(opponentId);
    } else {
        activeDuelState.match->setBountyId(opponentId);
    }
    memcpy(activeDuelState.opponentMac.data(), opponentMac, 6);

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_W(MATCH_MANAGER_TAG, "TIMING receiveMatch-ready T=%lu", clock ? clock->milliseconds() : 0UL);

    activeDuelState.matchIsReady = true;

    sendMatchAck();
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

    return player->isHunter() ?
    activeDuelState.match->getHunterDrawTime() < activeDuelState.match->getBountyDrawTime() :
    activeDuelState.match->getBountyDrawTime() < activeDuelState.match->getHunterDrawTime();
}

bool MatchManager::finalizeMatch() {
    if (!activeDuelState.match) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    // Snapshot times for the result screen (before clearCurrentMatch wipes the match).
    lastMatchDisplay_.myTimeMs = player->isHunter()
        ? activeDuelState.match->getHunterDrawTime()
        : activeDuelState.match->getBountyDrawTime();
    lastMatchDisplay_.opponentTimeMs = player->isHunter()
        ? activeDuelState.match->getBountyDrawTime()
        : activeDuelState.match->getHunterDrawTime();
    lastMatchDisplay_.hasData = true;

    std::string match_id = activeDuelState.match->getMatchId();

    // Save to storage
    if (appendMatchToStorage(&*activeDuelState.match)) {
        // Update stored count
        updateStoredMatchCount(getStoredMatchCount() + 1);
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
    storage->clear();
    updateStoredMatchCount(0);
    LOG_I("PDN", "Cleared match storage\n");
}

size_t MatchManager::getStoredMatchCount() {
    return storage->readUChar(PREF_COUNT_KEY, 0);
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
    if (storage->writeUChar("test_key", 123) != 1) {
        LOG_E(MATCH_MANAGER_TAG, "NVS Preference test write failed! Potential hardware or NVS issue");
    } else {
        uint8_t test_val = storage->readUChar("test_key", 0);
        LOG_W(MATCH_MANAGER_TAG, "NVS test write/read successful: wrote 123, read %d", test_val);
    }

    // Save match JSON to preferences
    if (storage->write(key, matchJson) != matchJson.length()) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to save match to storage - key: %s, length: %d", 
                key, matchJson.length());
        
        return false;
    }

    LOG_W(MATCH_MANAGER_TAG, "Successfully saved match to storage at index %d", count);
    return true;
}

void MatchManager::updateStoredMatchCount(uint8_t count) {
    if (storage->writeUChar(PREF_COUNT_KEY, count) != 1) {
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
    std::string matchJson = storage->read(key, "");
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

void MatchManager::initialize(Player* player, StorageInterface* storage, QuickdrawWirelessManager* quickdrawWirelessManager) {
    this->player = player;
    this->storage = storage;
    this->quickdrawWirelessManager = quickdrawWirelessManager;

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
        Player *player = matchManager->player;
        QuickdrawWirelessManager* quickdrawWirelessManager = matchManager->quickdrawWirelessManager;

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
        matchManager->lastMatchDisplay_.boostMs = boost;

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms (boost %lu) for %s",
                boostedTimeMs, boost, player->isHunter() ? "Hunter" : "Bounty");

        player->isHunter() ?
        matchManager->setHunterDrawTime(boostedTimeMs)
        : matchManager->setBountyDrawTime(boostedTimeMs);

        // Player stats record the raw (unboosted) reaction time — boost is a
        // duel-time advantage, not an achievement the player actually made.
        player->addReactionTime(reactionTimeMs);

        LOG_I(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        LOG_I(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s",
                MacToString(activeDuelState->opponentMac.data()));

        // Send the BOOSTED time in DRAW_RESULT so both sides agree on who won.
        QuickdrawCommand command(activeDuelState->opponentMac.data(), QDCommand::DRAW_RESULT, matchManager->getCurrentMatch()->getMatchId(), player->getUserID().c_str(), boostedTimeMs, player->isHunter());

        quickdrawWirelessManager->broadcastPacket(activeDuelState->opponentMac.data(), command);

        matchManager->setReceivedButtonPush();
        
        LOG_I(MATCH_MANAGER_TAG, "Reaction time: %lu ms", reactionTimeMs);
    };

    buttonMasher = [](void *ctx) {
        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        matchManager->activeDuelState.buttonMasherCount++;
        LOG_I(MATCH_MANAGER_TAG, "Button masher count: %d", matchManager->activeDuelState.buttonMasherCount);
    };
}

void MatchManager::sendMatchAck() {
    QuickdrawCommand command(activeDuelState.opponentMac.data(), QDCommand::MATCH_ID_ACK, activeDuelState.match->getMatchId(), player->getUserID().c_str(), 0, player->isHunter());
    quickdrawWirelessManager->broadcastPacket(activeDuelState.opponentMac.data(), command);
}

void MatchManager::sendMatchRoleMismatch() {
    QuickdrawCommand command(activeDuelState.opponentMac.data(), QDCommand::MATCH_ROLE_MISMATCH, activeDuelState.match->getMatchId(), player->getUserID().c_str(), 0, player->isHunter());
    quickdrawWirelessManager->broadcastPacket(activeDuelState.opponentMac.data(), command);
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
        if(player->isHunter() == command.isHunter) {
            // Same role on both sides — not a valid hunter/bounty pairing.
            sendMatchRoleMismatch();
            return;
        } else {
            receiveMatch(command.matchId, command.playerId, command.isHunter, const_cast<uint8_t*>(command.wifiMacAddr));
        }
    } else if(command.command == QDCommand::MATCH_ID_ACK) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ID_ACK command from opponent");
        if (!isFromActiveMatchOpponent(command)) return;
        if(player->isHunter()) { activeDuelState.match->setBountyId(command.playerId); }
        else { activeDuelState.match->setHunterId(command.playerId); }
        auto* clock = SimpleTimer::getPlatformClock();
        LOG_W(MATCH_MANAGER_TAG, "TIMING matchAck-ready T=%lu", clock ? clock->milliseconds() : 0UL);
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

    player->isHunter() ? setHunterDrawTime(pityTime) : setBountyDrawTime(pityTime);
    setNeverPressed();
    // Mirror what gets uploaded: every duel contributes a draw time to the server,
    // so on-device "average reaction" should include pity times too.
    player->addReactionTime(pityTime);

    QuickdrawCommand command(activeDuelState.opponentMac.data(), QDCommand::NEVER_PRESSED, activeDuelState.match->getMatchId(), player->getUserID().c_str(), pityTime, player->isHunter());
    quickdrawWirelessManager->broadcastPacket(activeDuelState.opponentMac.data(), command);
}

bool MatchManager::isMatchReady() {
    return activeDuelState.matchIsReady;
}