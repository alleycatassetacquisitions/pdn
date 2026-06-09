#include "game/match.hpp"
#include "id-generator.hpp"
#include "device/drivers/logger.hpp"
#include <ArduinoJson.h>
#include <cstring>

Match::Match(const char* mid, const char* player_id, bool isHunter)
    : hunter_draw_time_ms(0), bounty_draw_time_ms(0) {
    memcpy(match_id, mid, IdGenerator::UUID_BUFFER_SIZE - 1);
    match_id[IdGenerator::UUID_BUFFER_SIZE - 1] = '\0';
    if(isHunter) {
        memcpy(hunter, player_id, 4);
        hunter[4] = '\0';
    } else {
        memcpy(bounty, player_id, 4);
        bounty[4] = '\0';
    }
}

Match::Match(){
    match_id[0] = '\0';
    hunter[0]   = '\0';
    bounty[0]   = '\0';
    hunter_draw_time_ms = 0;
    bounty_draw_time_ms = 0;
    voided = false;
}

void Match::setHunterId(const char* hunter_id) {
    memcpy(hunter, hunter_id, 4);
    hunter[4] = '\0';
}

void Match::setBountyId(const char* bounty_id) {
    memcpy(bounty, bounty_id, 4);
    bounty[4] = '\0';
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
    matchObj[JSON_KEY_VOIDED] = voided;

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
            const char* v = doc[JSON_KEY_MATCH_ID].as<const char*>();
            strncpy(match_id, v ? v : "", IdGenerator::UUID_BUFFER_SIZE - 1);
            match_id[IdGenerator::UUID_BUFFER_SIZE - 1] = '\0';
        }
        if (doc[JSON_KEY_HUNTER_ID].is<const char*>()) {
            const char* v = doc[JSON_KEY_HUNTER_ID].as<const char*>();
            strncpy(hunter, v ? v : "", 4);
            hunter[4] = '\0';
        }
        if (doc[JSON_KEY_BOUNTY_ID].is<const char*>()) {
            const char* v = doc[JSON_KEY_BOUNTY_ID].as<const char*>();
            strncpy(bounty, v ? v : "", 4);
            bounty[4] = '\0';
        }
        if (doc[JSON_KEY_HUNTER_TIME].is<unsigned long>()) {
            hunter_draw_time_ms = doc[JSON_KEY_HUNTER_TIME].as<unsigned long>();
        }
        if (doc[JSON_KEY_BOUNTY_TIME].is<unsigned long>()) {
            bounty_draw_time_ms = doc[JSON_KEY_BOUNTY_TIME].as<unsigned long>();
        }
        if (doc[JSON_KEY_VOIDED].is<bool>()) {
            voided = doc[JSON_KEY_VOIDED].as<bool>();
        }
    } else {
        // LOG_E("Match", "Failed to parse JSON: %s", error.c_str());
    }
}

