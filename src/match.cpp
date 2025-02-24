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
    // Create a JSON object for the match
    StaticJsonDocument<128> doc;
    JsonObject matchObj = doc.to<JsonObject>();
    matchObj["match_id"] = match_id;
    matchObj["hunter"] = hunter;
    matchObj["bounty"] = bounty;
    matchObj["capture"] = winner_is_hunter;
    matchObj["hunter_time"] = hunter_draw_time_ms;
    matchObj["bounty_time"] = bounty_draw_time_ms;

    // Serialize the JSON object to a string
    string json;
    serializeJson(matchObj, json);
    return json;
}

void Match::fromJson(const string &json) {
    // Parse the JSON string into a JSON object
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (!error) {
        // Only update fields if they exist in the JSON
        if (doc.containsKey("match_id")) {
            match_id = doc["match_id"].as<string>();
        }
        if (doc.containsKey("hunter")) {
            hunter = doc["hunter"].as<string>();
        }
        if (doc.containsKey("bounty")) {
            bounty = doc["bounty"].as<string>();
        }
        if (doc.containsKey("capture")) {
            winner_is_hunter = doc["capture"];
        }
        if (doc.containsKey("hunter_time")) {
            hunter_draw_time_ms = doc["hunter_time"].as<unsigned long>();
        }
        if (doc.containsKey("bounty_time")) {
            bounty_draw_time_ms = doc["bounty_time"].as<unsigned long>();
        }
    } else {
      // Serial.println("Failed to parse JSON");
    }
}

size_t Match::serialize(uint8_t* buffer) const {
    size_t currentPos = 0;
    uint8_t uuidBytes[UUID_BINARY_SIZE];
    
    // Serialize match_id
    IdGenerator::GetInstance()->uuidStringToBytes(match_id, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, UUID_BINARY_SIZE);
    currentPos += UUID_BINARY_SIZE;
    
    // Serialize hunter id
    IdGenerator::GetInstance()->uuidStringToBytes(hunter, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, UUID_BINARY_SIZE);
    currentPos += UUID_BINARY_SIZE;
    
    // Serialize bounty id
    IdGenerator::GetInstance()->uuidStringToBytes(bounty, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, UUID_BINARY_SIZE);
    currentPos += UUID_BINARY_SIZE;
    
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
    uint8_t uuidBytes[UUID_BINARY_SIZE];
    
    // Deserialize match_id
    memcpy(uuidBytes, buffer + currentPos, UUID_BINARY_SIZE);
    match_id = IdGenerator::GetInstance()->uuidBytesToString(uuidBytes);
    currentPos += UUID_BINARY_SIZE;
    
    // Deserialize hunter id
    memcpy(uuidBytes, buffer + currentPos, UUID_BINARY_SIZE);
    hunter = IdGenerator::GetInstance()->uuidBytesToString(uuidBytes);
    currentPos += UUID_BINARY_SIZE;
    
    // Deserialize bounty id
    memcpy(uuidBytes, buffer + currentPos, UUID_BINARY_SIZE);
    bounty = IdGenerator::GetInstance()->uuidBytesToString(uuidBytes);
    currentPos += UUID_BINARY_SIZE;
    
    // Deserialize winner flag
    winner_is_hunter = buffer[currentPos++] == 1;
    
    // Deserialize draw times
    memcpy(&hunter_draw_time_ms, buffer + currentPos, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    memcpy(&bounty_draw_time_ms, buffer + currentPos, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    
    return currentPos;
}