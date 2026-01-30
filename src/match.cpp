#include "game/match.hpp"
#include "id-generator.hpp"
#include "device/drivers/logger.hpp"
#include <ArduinoJson.h>
#include <string.h> // for memcpy

Match::Match(std::string match_id, std::string hunter_id, std::string bounty_id) 
    : match_id(match_id), hunter(hunter_id), bounty(bounty_id), hunter_draw_time_ms(0), bounty_draw_time_ms(0) {
}

Match::Match(){
    setupMatch("", "", "");
}

void Match::setupMatch(std::string id, std::string hunter, std::string bounty)
{
    match_id = id;
    this->hunter = hunter;
    this->bounty = bounty;
    hunter_draw_time_ms = 0;  // Reset draw times
    bounty_draw_time_ms = 0;
}


void Match::setHunterDrawTime(unsigned long timeMs) {
    hunter_draw_time_ms = timeMs;
}

void Match::setBountyDrawTime(unsigned long timeMs) {
    bounty_draw_time_ms = timeMs;
}

std::string Match::toJson() const
{
    // LOG_I("Match", "Serializing JSON: %s", match_id.c_str());
    // LOG_I("Match", "Hunter: %s", hunter.c_str());
    // LOG_I("Match", "Bounty: %s", bounty.c_str());
    // LOG_I("Match", "Hunter draw time: %lu", hunter_draw_time_ms);
    // LOG_I("Match", "Bounty draw time: %lu", bounty_draw_time_ms);

    JsonDocument doc;
    JsonObject matchObj = doc.to<JsonObject>();
    matchObj[JSON_KEY_MATCH_ID] = match_id;
    matchObj[JSON_KEY_HUNTER_ID] = hunter;
    matchObj[JSON_KEY_BOUNTY_ID] = bounty;
    matchObj[JSON_KEY_WINNER_IS_HUNTER] = hunter_draw_time_ms < bounty_draw_time_ms;
    matchObj[JSON_KEY_HUNTER_TIME] = hunter_draw_time_ms;
    matchObj[JSON_KEY_BOUNTY_TIME] = bounty_draw_time_ms;

    std::string json;
    serializeJson(matchObj, json);

    // LOG_I("Match", "Serialized JSON: %s", json.c_str());
    return json;
}

void Match::fromJson(const std::string &json) {
    // LOG_I("Match", "Deserializing JSON: %s", json.c_str());
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (!error) {
        if (doc[JSON_KEY_MATCH_ID].is<const char*>()) {
            match_id = doc[JSON_KEY_MATCH_ID].as<std::string>();
        }
        if (doc[JSON_KEY_HUNTER_ID].is<const char*>()) {
            hunter = doc[JSON_KEY_HUNTER_ID].as<std::string>();
        }
        if (doc[JSON_KEY_BOUNTY_ID].is<const char*>()) {
            bounty = doc[JSON_KEY_BOUNTY_ID].as<std::string>();
        }
        if (doc[JSON_KEY_HUNTER_TIME].is<unsigned long>()) {
            hunter_draw_time_ms = doc[JSON_KEY_HUNTER_TIME].as<unsigned long>();
        }
        if (doc[JSON_KEY_BOUNTY_TIME].is<unsigned long>()) {
            bounty_draw_time_ms = doc[JSON_KEY_BOUNTY_TIME].as<unsigned long>();
        }
    } else {
        // LOG_E("Match", "Failed to parse JSON: %s", error.c_str());
    }
}

size_t Match::serialize(uint8_t* buffer) const {
    size_t currentPos = 0;
    uint8_t uuidBytes[IdGenerator::UUID_BINARY_SIZE];
    
    // Serialize match_id
    IdGenerator::uuidStringToBytes(match_id, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, IdGenerator::UUID_BINARY_SIZE);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Serialize hunter id
    IdGenerator::uuidStringToBytes(hunter, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, IdGenerator::UUID_BINARY_SIZE);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Serialize bounty id
    IdGenerator::uuidStringToBytes(bounty, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, IdGenerator::UUID_BINARY_SIZE);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Serialize draw times
    memcpy(buffer + currentPos, &hunter_draw_time_ms, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    memcpy(buffer + currentPos, &bounty_draw_time_ms, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    
    return currentPos;
}

size_t Match::deserialize(const uint8_t* buffer) {
    size_t currentPos = 0;
    uint8_t uuidBytes[IdGenerator::UUID_BINARY_SIZE];
    
    // Deserialize match_id
    memcpy(uuidBytes, buffer + currentPos, IdGenerator::UUID_BINARY_SIZE);
    match_id = IdGenerator::uuidBytesToString(uuidBytes);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Deserialize hunter id
    memcpy(uuidBytes, buffer + currentPos, IdGenerator::UUID_BINARY_SIZE);
    hunter = IdGenerator::uuidBytesToString(uuidBytes);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Deserialize bounty id
    memcpy(uuidBytes, buffer + currentPos, IdGenerator::UUID_BINARY_SIZE);
    bounty = IdGenerator::uuidBytesToString(uuidBytes);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Deserialize draw times
    memcpy(&hunter_draw_time_ms, buffer + currentPos, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    memcpy(&bounty_draw_time_ms, buffer + currentPos, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    
    return currentPos;
}