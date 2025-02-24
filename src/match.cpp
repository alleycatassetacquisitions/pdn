#include "../lib/pdn-libs/match.hpp"
#include "id-generator.hpp"
#include <ArduinoJson.h>
#include <string.h> // for memcpy

Match::Match(string match_id, string hunter_id, string bounty_id) 
    : match_id(match_id), hunter(hunter_id), bounty(bounty_id), 
      winner_is_hunter(false), hunter_draw_time_ms(0), bounty_draw_time_ms(0) {
}

void Match::setupMatch(string id, string hunter, string bounty)
{
    match_id = id;
    this->hunter = hunter;
    this->bounty = bounty;
    winner_is_hunter = false; // Reset winner state on setup
    hunter_draw_time_ms = 0;  // Reset draw times
    bounty_draw_time_ms = 0;
}

void Match::setWinner(bool winner_is_hunter)
{
    this->winner_is_hunter = winner_is_hunter;
}

void Match::setHunterDrawTime(unsigned long timeMs) {
    hunter_draw_time_ms = timeMs;
}

void Match::setBountyDrawTime(unsigned long timeMs) {
    bounty_draw_time_ms = timeMs;
}

string Match::toJson() const
{
    JsonDocument doc;
    JsonObject matchObj = doc.to<JsonObject>();
    matchObj[JSON_KEY_MATCH_ID] = match_id;
    matchObj[JSON_KEY_HUNTER_ID] = hunter;
    matchObj[JSON_KEY_BOUNTY_ID] = bounty;
    matchObj[JSON_KEY_WINNER_IS_HUNTER] = winner_is_hunter;
    matchObj[JSON_KEY_HUNTER_TIME] = hunter_draw_time_ms;
    matchObj[JSON_KEY_BOUNTY_TIME] = bounty_draw_time_ms;

    string json;
    serializeJson(matchObj, json);
    return json;
}

void Match::fromJson(const string &json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (!error) {
        if (doc[JSON_KEY_MATCH_ID].is<const char*>()) {
            match_id = doc[JSON_KEY_MATCH_ID].as<string>();
        }
        if (doc[JSON_KEY_HUNTER_ID].is<const char*>()) {
            hunter = doc[JSON_KEY_HUNTER_ID].as<string>();
        }
        if (doc[JSON_KEY_BOUNTY_ID].is<const char*>()) {
            bounty = doc[JSON_KEY_BOUNTY_ID].as<string>();
        }
        if (doc[JSON_KEY_WINNER_IS_HUNTER].is<bool>()) {
            winner_is_hunter = doc[JSON_KEY_WINNER_IS_HUNTER];
        }
        if (doc[JSON_KEY_HUNTER_TIME].is<unsigned long>()) {
            hunter_draw_time_ms = doc[JSON_KEY_HUNTER_TIME].as<unsigned long>();
        }
        if (doc[JSON_KEY_BOUNTY_TIME].is<unsigned long>()) {
            bounty_draw_time_ms = doc[JSON_KEY_BOUNTY_TIME].as<unsigned long>();
        }
    } else {
        ESP_LOGE("Match", "Failed to parse JSON");
    }
}

size_t Match::serialize(uint8_t* buffer) const {
    size_t currentPos = 0;
    uint8_t uuidBytes[IdGenerator::UUID_BINARY_SIZE];
    
    // Serialize match_id
    IdGenerator::GetInstance()->uuidStringToBytes(match_id, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, IdGenerator::UUID_BINARY_SIZE);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Serialize hunter id
    IdGenerator::GetInstance()->uuidStringToBytes(hunter, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, IdGenerator::UUID_BINARY_SIZE);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Serialize bounty id
    IdGenerator::GetInstance()->uuidStringToBytes(bounty, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, IdGenerator::UUID_BINARY_SIZE);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Serialize winner flag
    buffer[currentPos++] = winner_is_hunter ? 1 : 0;
    
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
    match_id = IdGenerator::GetInstance()->uuidBytesToString(uuidBytes);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Deserialize hunter id
    memcpy(uuidBytes, buffer + currentPos, IdGenerator::UUID_BINARY_SIZE);
    hunter = IdGenerator::GetInstance()->uuidBytesToString(uuidBytes);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Deserialize bounty id
    memcpy(uuidBytes, buffer + currentPos, IdGenerator::UUID_BINARY_SIZE);
    bounty = IdGenerator::GetInstance()->uuidBytesToString(uuidBytes);
    currentPos += IdGenerator::UUID_BINARY_SIZE;
    
    // Deserialize winner flag
    winner_is_hunter = buffer[currentPos++] == 1;
    
    // Deserialize draw times
    memcpy(&hunter_draw_time_ms, buffer + currentPos, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    memcpy(&bounty_draw_time_ms, buffer + currentPos, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    
    return currentPos;
}