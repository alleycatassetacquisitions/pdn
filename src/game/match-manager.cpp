#include "game/match-manager.hpp"
#include "id-generator.hpp"
#include <ArduinoJson.h>

MatchManager::~MatchManager() {
    delete activeMatch;
    activeMatch = nullptr;
}

Match* MatchManager::createMatch(const string& hunter_id, const string& bounty_id) {
    // Only allow one active match at a time
    if (activeMatch != nullptr) {
        return nullptr;
    }

    // Generate a new UUID for the match
    string match_id = IdGenerator::GetInstance()->generateId();
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
        return false;
    }

    // Save to EEPROM
    if (appendMatchToEEPROM(activeMatch)) {
        // Update stored count
        updateStoredMatchCount(getStoredMatchCount() + 1);
        
        // Clear active match
        delete activeMatch;
        activeMatch = nullptr;
        return true;
    }
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

string MatchManager::toJson() const {
    // Create JSON array
    StaticJsonDocument<2048> doc;  // Adjust size based on max matches
    JsonArray matchArray = doc.to<JsonArray>();

    // Read all matches from EEPROM
    uint8_t count = getStoredMatchCount();
    for (uint8_t i = 0; i < count; i++) {
        Match* match = readMatchFromEEPROM(i);
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
    updateStoredMatchCount(0);
}

size_t MatchManager::getStoredMatchCount() const {
    return EEPROM.read(EEPROM_MATCH_COUNT_ADDR);
}

bool MatchManager::appendMatchToEEPROM(const Match* match) {
    if (!match) return false;

    uint8_t count = getStoredMatchCount();
    if (count >= MAX_MATCHES) return false;

    // Calculate address for new match
    size_t addr = EEPROM_MATCHES_START_ADDR + (count * MATCH_BINARY_SIZE);
    
    // Check if we have enough space
    if (addr + MATCH_BINARY_SIZE > EEPROM.length()) {
        return false;
    }

    // Serialize and save match
    uint8_t buffer[MATCH_BINARY_SIZE];
    match->serialize(buffer);
    
    for (size_t i = 0; i < MATCH_BINARY_SIZE; i++) {
        EEPROM.write(addr + i, buffer[i]);
    }
    
    return true;
}

void MatchManager::updateStoredMatchCount(uint8_t count) {
    EEPROM.write(EEPROM_MATCH_COUNT_ADDR, count);
}

Match* MatchManager::readMatchFromEEPROM(uint8_t index) const {
    if (index >= getStoredMatchCount()) {
        return nullptr;
    }

    // Calculate address for match
    size_t addr = EEPROM_MATCHES_START_ADDR + (index * MATCH_BINARY_SIZE);
    
    // Read match data
    uint8_t buffer[MATCH_BINARY_SIZE];
    for (size_t i = 0; i < MATCH_BINARY_SIZE; i++) {
        buffer[i] = EEPROM.read(addr + i);
    }

    // Create and deserialize match
    Match* match = new Match();
    if (match->deserialize(buffer) > 0) {
        return match;
    }
    
    delete match;
    return nullptr;
}
