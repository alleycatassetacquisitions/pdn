#include "game/match-manager.hpp"
#include <ArduinoJson.h>
#include "device/drivers/logger.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "device/device-constants.hpp"

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

MatchManager::MatchManager() 
    : player(nullptr)
    , storage(nullptr)
    , peerComms(nullptr)
    , quickdrawWirelessManager(nullptr) {
}

MatchManager::~MatchManager() { 
    delete activeDuelState.match;
    activeDuelState.match = nullptr;
    quickdrawWirelessManager = nullptr;
    if (storage) {
        storage->end();
    }
}

void MatchManager::clearCurrentMatch() {
    if (activeDuelState.match) {
        if (peerComms && player && !player->getOpponentMacAddress().empty()) {
            uint8_t mac[6];
            if (StringToMac(player->getOpponentMacAddress().c_str(), mac)) {
                peerComms->removePeer(mac);
                LOG_I(MATCH_MANAGER_TAG, "Removed opponent peer from ESP-NOW");
            }
        }

        LOG_I(MATCH_MANAGER_TAG, "Clearing current match");
        delete activeDuelState.match;
        activeDuelState.match = nullptr;
        activeDuelState.hasReceivedDrawResult = false;
        activeDuelState.hasPressedButton = false;
        activeDuelState.duelLocalStartTime = 0;
        activeDuelState.gracePeriodExpiredNoResult = false;
    }
}

Match* MatchManager::createMatch(const std::string& match_id, const std::string& hunter_id, const std::string& bounty_id) {
    // Only allow one active match at a time
    if (activeDuelState.match != nullptr) {
        LOG_W(MATCH_MANAGER_TAG, "Cannot create match %s - active match already exists (ID: %s)",
              match_id.c_str(), activeDuelState.match->getMatchId().c_str());
        return nullptr;
    }

    activeDuelState.match = new Match(match_id, hunter_id, bounty_id);
    return activeDuelState.match;
}

Match* MatchManager::receiveMatch(const Match& match) {
    // Only allow one active match at a time
    if (activeDuelState.match != nullptr) {
        LOG_W(MATCH_MANAGER_TAG, "Cannot receive match %s - active match already exists (ID: %s)",
              match.getMatchId().c_str(), activeDuelState.match->getMatchId().c_str());
        return nullptr;
    }

    // Create a new Match object on the heap instead of storing a pointer to the parameter
    activeDuelState.match = new Match(match.getMatchId(), match.getHunterId(), match.getBountyId());

    // Copy draw times
    activeDuelState.match->setHunterDrawTime(match.getHunterDrawTime());
    activeDuelState.match->setBountyDrawTime(match.getBountyDrawTime());

    return activeDuelState.match;
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
       
    return (activeDuelState.hasReceivedDrawResult && activeDuelState.hasPressedButton)
    || (activeDuelState.gracePeriodExpiredNoResult && activeDuelState.hasPressedButton);
}

void MatchManager::setNeverPressed() {
    activeDuelState.gracePeriodExpiredNoResult = true;
}

bool MatchManager::didWin() {
    if (!activeDuelState.match) {
        return false;
    }

    if(player->isHunter() && activeDuelState.match->getBountyDrawTime() == 0) {
        return true;
    }

    if(!player->isHunter() && activeDuelState.match->getHunterDrawTime() == 0) {
        return true;
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

    std::string match_id = activeDuelState.match->getMatchId();

    // Save to storage
    if (appendMatchToStorage(activeDuelState.match)) {
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

void MatchManager::initialize(Player* player, StorageInterface* storage, PeerCommsInterface* peerComms, QuickdrawWirelessManager* quickdrawWirelessManager) {
    this->player = player;
    this->storage = storage;
    this->peerComms = peerComms;
    this->quickdrawWirelessManager = quickdrawWirelessManager;

    duelButtonPush = [](void *ctx) {
        unsigned long now = SimpleTimer::getPlatformClock()->milliseconds();
        
        if (!ctx) {
            LOG_E(MATCH_MANAGER_TAG, "Button press handler received null context");
            return;
        }

        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        ActiveDuelState* activeDuelState = &matchManager->activeDuelState;
        Player *player = matchManager->player;
        QuickdrawWirelessManager* quickdrawWirelessManager = matchManager->quickdrawWirelessManager;

        if(matchManager->getHasPressedButton()) {
            LOG_I(MATCH_MANAGER_TAG, "Button already pressed - skipping");
            return;
        }

        if(matchManager->getCurrentMatch() == nullptr) {
            LOG_E(MATCH_MANAGER_TAG, "No current match - skipping");
            return;
        }

        unsigned long reactionTimeMs = now - matchManager->getDuelLocalStartTime();

        if(activeDuelState->buttonMasherCount > 0) {
            reactionTimeMs = reactionTimeMs + (activeDuelState->BUTTON_MASHER_PENALTY_MS * activeDuelState->buttonMasherCount);
        }

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms for %s", 
                reactionTimeMs, player->isHunter() ? "Hunter" : "Bounty");

        player->isHunter() ? 
        matchManager->setHunterDrawTime(reactionTimeMs) 
        : matchManager->setBountyDrawTime(reactionTimeMs);

        LOG_I(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        // Send a packet with the reaction time
        if (player->getOpponentMacAddress().empty()) {
            LOG_E(MATCH_MANAGER_TAG, "Cannot send packet - opponent MAC address is empty");
            return;
        }

        LOG_I(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s", 
                player->getOpponentMacAddress().c_str());
                
        quickdrawWirelessManager->broadcastPacket(
            player->getOpponentMacAddress(),
            QDCommand::DRAW_RESULT,
            *matchManager->getCurrentMatch()
        );

        matchManager->setReceivedButtonPush();
        
        LOG_I(MATCH_MANAGER_TAG, "Reaction time: %lu ms", reactionTimeMs);
    };

    buttonMasher = [](void *ctx) {
        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        matchManager->activeDuelState.buttonMasherCount++;
        LOG_I(MATCH_MANAGER_TAG, "Button masher count: %d", matchManager->activeDuelState.buttonMasherCount);
    };
}

void MatchManager::listenForMatchResults(QuickdrawCommand command) {
    LOG_I(MATCH_MANAGER_TAG, "Received command: %d", command.command);
    
    if(command.command == QDCommand::DRAW_RESULT || command.command == QDCommand::NEVER_PRESSED) {
        LOG_I(MATCH_MANAGER_TAG, "Received DRAW_RESULT command from opponent");
        
        long opponentTime = player->isHunter() ? 
            command.match.getBountyDrawTime() : 
            command.match.getHunterDrawTime();
            
        LOG_I(MATCH_MANAGER_TAG, "Opponent reaction time: %ld ms", opponentTime);
        
        player->isHunter() ? 
        setBountyDrawTime(command.match.getBountyDrawTime()) 
        : setHunterDrawTime(command.match.getHunterDrawTime());

        setReceivedDrawResult();
    } else {
        LOG_W(MATCH_MANAGER_TAG, "Received unexpected command in Match Manager: %d", command.command);
    }
}
