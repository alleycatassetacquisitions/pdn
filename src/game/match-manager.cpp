#include "game/match-manager.hpp"
#include "id-generator.hpp"
#include <ArduinoJson.h>

MatchManager::MatchManager() : activeMatch(nullptr) {
    // Open preferences with our namespace
    if (!prefs.begin(PREF_NAMESPACE, false)) {
        ESP_LOGE("PDN", "Failed to initialize preferences\n");
    }
}

MatchManager::~MatchManager() {
    delete activeMatch;
    activeMatch = nullptr;
    prefs.end();
}

Match* MatchManager::createMatch(const string& hunter_id, const string& bounty_id) {
    // Only allow one active match at a time
    if (activeMatch != nullptr) {
        return nullptr;
    }

    // Generate a new UUID for the match
    char* uuid = IdGenerator::GetInstance()->generateId();
    string match_id(uuid);  // Properly construct string from char*
    activeMatch = new Match(match_id, hunter_id, bounty_id);
    return activeMatch;
}

Match* MatchManager::receiveMatch(const string& match_json) {
    // Only allow one active match at a time
    if (activeMatch != nullptr) {
        return nullptr;
    }

    // Parse the JSON into a new match
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, match_json);
    if (error) {
        return nullptr;
    }

    activeMatch = new Match();
    activeMatch->fromJson(match_json);
    return activeMatch;
}

bool MatchManager::finalizeMatch(const string& match_id) {
    if (!activeMatch || activeMatch->getMatchId() != match_id) {
        ESP_LOGE("PDN", "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    // Save to storage
    if (appendMatchToStorage(activeMatch)) {
        // Update stored count
        updateStoredMatchCount(getStoredMatchCount() + 1);
        
        // Clear active match
        delete activeMatch;
        activeMatch = nullptr;
        ESP_LOGI("PDN", "Successfully finalized match %s\n", match_id.c_str());
        return true;
    }
    
    ESP_LOGE("PDN", "Failed to finalize match %s\n", match_id.c_str());
    return false;
}

bool MatchManager::updateMatch(const string& match_id, bool winner_is_hunter,
                             unsigned long hunter_time_ms, unsigned long bounty_time_ms) {
    if (!activeMatch || activeMatch->getMatchId() != match_id) {
        return false;
    }

    activeMatch->setWinner(winner_is_hunter);
    activeMatch->setHunterDrawTime(hunter_time_ms);
    activeMatch->setBountyDrawTime(bounty_time_ms);
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
    
    // Save match JSON to preferences
    if (!prefs.putString(key, matchJson.c_str())) {
        ESP_LOGE("PDN", "Failed to save match to storage\n");
        return false;
    }

    ESP_LOGI("PDN", "Successfully saved match to storage at index %d\n", count);
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
