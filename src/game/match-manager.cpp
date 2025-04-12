#include "game/match-manager.hpp"
#include <ArduinoJson.h>
#include <esp_log.h>
#include "wireless/quickdraw-wireless-manager.hpp"

#define MATCH_MANAGER_TAG "MATCH_MANAGER"

MatchManager* MatchManager::GetInstance() {
    static bool firstCall = true;
    static MatchManager instance;
    
    if (firstCall) {
        ESP_LOGW("MATCH_MANAGER", "***** MatchManager singleton initialized for the first time *****");
        firstCall = false;
    }
    
    return &instance;
}

MatchManager::MatchManager() {}

MatchManager::~MatchManager() {
    delete activeDuelState.match;
    activeDuelState.match = nullptr;
    prefs.end();
}

void MatchManager::clearCurrentMatch() {
    if (activeDuelState.match) {
        ESP_LOGI("PDN", "Clearing current match");
        delete activeDuelState.match;
        activeDuelState.match = nullptr;
        activeDuelState.hasReceivedDrawResult = false;
        activeDuelState.hasPressedButton = false;
        activeDuelState.duelLocalStartTime = 0;
        activeDuelState.gracePeriodExpiredNoResult = false;
    }
}

Match* MatchManager::createMatch(const string& match_id, const string& hunter_id, const string& bounty_id) {
    // Only allow one active match at a time
    if (activeDuelState.match != nullptr) {
        return nullptr;
    }

    activeDuelState.match = new Match(match_id, hunter_id, bounty_id);
    return activeDuelState.match;
}

Match* MatchManager::receiveMatch(Match match) {
    // Only allow one active match at a time
    if (activeDuelState.match != nullptr) {
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
        ESP_LOGE("PDN", "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    string match_id = activeDuelState.match->getMatchId();

    // Save to storage
    if (appendMatchToStorage(activeDuelState.match)) {
        // Update stored count
        updateStoredMatchCount(getStoredMatchCount() + 1);
        clearCurrentMatch();
        ESP_LOGI("PDN", "Successfully finalized match %s\n", match_id.c_str());
        return true;
    }
    
    ESP_LOGE("PDN", "Failed to finalize match %s\n", match_id.c_str());
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

string MatchManager::toJson() {
    // Create JSON document with an object at the root
    StaticJsonDocument<2048> doc;  // Adjust size based on max matches
    
    // Create a "matches" array within the root object
    JsonArray matchArray = doc.createNestedArray("matches");

    // Read all matches from storage
    uint8_t count = getStoredMatchCount();
    for (uint8_t i = 0; i < count; i++) {
        Match* match = readMatchFromStorage(i);
        if (match) {
            // Add match to array
            string matchJson = match->toJson();
            StaticJsonDocument<256> matchDoc;
            deserializeJson(matchDoc, matchJson);
            matchArray.add(matchDoc.as<JsonObject>());
            delete match;
        }
    }

    // Serialize to string
    string output;
    serializeJson(doc, output);
    return output;
}

void MatchManager::clearStorage() {
    prefs.clear();
    updateStoredMatchCount(0);
    ESP_LOGI("PDN", "Cleared match storage\n");
}

size_t MatchManager::getStoredMatchCount() {
    return prefs.getUChar(PREF_COUNT_KEY, 0);
}

bool MatchManager::appendMatchToStorage(const Match* match) {
    if (!match) return false;

    uint8_t count = getStoredMatchCount();
    if (count >= MAX_MATCHES) {
        ESP_LOGE("PDN", "Cannot save match - storage full\n");
        return false;
    }

    // Convert match to JSON for storage
    string matchJson = match->toJson();
    
    // Create key for this match
    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, count);

    ESP_LOGW(MATCH_MANAGER_TAG, "Attempting to save match to storage at key %s (JSON length: %d bytes)", 
            key, matchJson.length());
    ESP_LOGW(MATCH_MANAGER_TAG, "Match JSON: %s", matchJson.c_str());
    
    // Try to check if preferences is working
    if (prefs.putUChar("test_key", 123) != 1) {
        ESP_LOGE(MATCH_MANAGER_TAG, "NVS Preference test write failed! Potential hardware or NVS issue");
    } else {
        uint8_t test_val = prefs.getUChar("test_key", 0);
        ESP_LOGW(MATCH_MANAGER_TAG, "NVS test write/read successful: wrote 123, read %d", test_val);
    }
    
    // Save match JSON to preferences
    if (!prefs.putString(key, matchJson.c_str())) {
        ESP_LOGE(MATCH_MANAGER_TAG, "Failed to save match to storage - key: %s, length: %d", 
                key, matchJson.length());
        
        return false;
    }

    ESP_LOGW(MATCH_MANAGER_TAG, "Successfully saved match to storage at index %d", count);
    return true;
}

void MatchManager::updateStoredMatchCount(uint8_t count) {
    if (!prefs.putUChar(PREF_COUNT_KEY, count)) {
        ESP_LOGE("PDN", "Failed to update match count\n");
    } else {
        ESP_LOGI("PDN", "Updated stored match count to %d\n", count);
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
    string matchJson = prefs.getString(key, "").c_str();
    if (matchJson.empty()) {
        return nullptr;
    }

    // Create and deserialize match
    Match* match = new Match();
    match->fromJson(matchJson);
    return match;
}

void MatchManager::initialize(Player* player) {
    this->player = player;

     // Open preferences with our namespace
    if (!prefs.begin(PREF_NAMESPACE, false)) {
        ESP_LOGW(MATCH_MANAGER_TAG, "Failed to initialize preferences");
    } else {
        ESP_LOGW(MATCH_MANAGER_TAG, "Preferences initialized successfully");
    }

    duelButtonPush = [](void *ctx) {
        unsigned long now = millis();
        
        if (!ctx) {
            ESP_LOGE(MATCH_MANAGER_TAG, "Button press handler received null context");
            return;
        }

        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        Player *player = matchManager->player;

        if(matchManager->getHasPressedButton()) {
            ESP_LOGI(MATCH_MANAGER_TAG, "Button already pressed - skipping");
            return;
        }

        if(matchManager->getCurrentMatch() == nullptr) {
            ESP_LOGE(MATCH_MANAGER_TAG, "No current match - skipping");
            return;
        }

        unsigned long reactionTimeMs = now - matchManager->getDuelLocalStartTime();

        ESP_LOGI(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms for %s", 
                reactionTimeMs, player->isHunter() ? "Hunter" : "Bounty");

        player->isHunter() ? 
        matchManager->setHunterDrawTime(reactionTimeMs) 
        : matchManager->setBountyDrawTime(reactionTimeMs);

        ESP_LOGI(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        // Send a packet with the reaction time
        if (!player->getOpponentMacAddress()) {
            ESP_LOGE(MATCH_MANAGER_TAG, "Cannot send packet - opponent MAC address is null");
            return;
        }

        ESP_LOGI(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s", 
                player->getOpponentMacAddress()->c_str());
                
        QuickdrawWirelessManager::GetInstance()->broadcastPacket(
            *player->getOpponentMacAddress(),
            QDCommand::DRAW_RESULT,
            *MatchManager::GetInstance()->getCurrentMatch()
        );

        matchManager->setReceivedButtonPush();
        
        ESP_LOGI(MATCH_MANAGER_TAG, "Reaction time: %lu ms", reactionTimeMs);
    };
}

void MatchManager::listenForMatchResults(QuickdrawCommand command) {
    ESP_LOGI(MATCH_MANAGER_TAG, "Received command: %d", command.command);
    
    if(command.command == QDCommand::DRAW_RESULT || command.command == QDCommand::NEVER_PRESSED) {
        ESP_LOGI(MATCH_MANAGER_TAG, "Received DRAW_RESULT command from opponent");
        
        long opponentTime = player->isHunter() ? 
            command.match.getBountyDrawTime() : 
            command.match.getHunterDrawTime();
            
        ESP_LOGI(MATCH_MANAGER_TAG, "Opponent reaction time: %ld ms", opponentTime);
        
        player->isHunter() ? 
        setBountyDrawTime(command.match.getBountyDrawTime()) 
        : setHunterDrawTime(command.match.getHunterDrawTime());

        setReceivedDrawResult();
    } else {
        ESP_LOGW(MATCH_MANAGER_TAG, "Received unexpected command in Match Manager: %d", command.command);
    }
}
