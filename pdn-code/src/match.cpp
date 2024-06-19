
#include <ArduinoJson.h>

#include "../include/match.hpp"
#include "match.hpp"

void Match::setupMatch(UUID id_generator, String hunter, String bounty)
{
    id_generator.generate();
    match_id = id_generator.toCharArray();

    this->hunter = hunter;
    this->bounty = bounty;
}

void Match::setupMatch(String id, String hunter, String bounty)
{
    match_id = id;
    this->hunter = hunter;
    this->bounty = bounty;
}

void Match::setWinner(bool winner_is_hunter)
{
    this->winner_is_hunter = winner_is_hunter;
}

String Match::toJson() const
{
    // Create a JSON object for the match
    StaticJsonDocument<128> doc;
    JsonObject matchObj = doc.to<JsonObject>();
    matchObj["match_id"] = match_id;
    matchObj["hunter"] = hunter;
    matchObj["bounty"] = bounty;
    matchObj["capture"] = winner_is_hunter;

    // Serialize the JSON object to a string
    String json;
    serializeJson(matchObj, json);
    return json;
}

void Match::fromJson(const String &json) {
    // Parse the JSON string into a JSON object
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, json);

    // Check if parsing was successful
    if (!error) {
      // Retrieve values from the JSON object
      match_id = doc["match_id"].as<String>();
      hunter = doc["hunter"].as<String>();
      bounty = doc["bounty"].as<String>();
      winner_is_hunter = doc["capture"];
    } else {
      // Serial.println("Failed to parse JSON");
    }
  }

void Match::fillJsonObject(JsonObject& matchObj)
{
    matchObj["match_id"] = match_id;
    matchObj["hunter"] = hunter;
    matchObj["bounty"] = bounty;
    matchObj["capture"] = winner_is_hunter;
}
