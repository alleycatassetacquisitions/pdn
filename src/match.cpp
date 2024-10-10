#include "../lib/pdn-libs/match.hpp"

#include <ArduinoJson.h>

void Match::setupMatch(string id, string hunter, string bounty)
{
    match_id = id;
    this->hunter = hunter;
    this->bounty = bounty;
}

void Match::setWinner(bool winner_is_hunter)
{
    this->winner_is_hunter = winner_is_hunter;
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

    // Serialize the JSON object to a string
    string json;
    serializeJson(matchObj, json);
    return json;
}

void Match::fromJson(const string &json) {
    // Parse the JSON string into a JSON object
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, json);

    // Check if parsing was successful
    if (!error) {
      // Retrieve values from the JSON object
      match_id = doc["match_id"].as<string>();
      hunter = doc["hunter"].as<string>();
      bounty = doc["bounty"].as<string>();
      winner_is_hunter = doc["capture"];
    } else {
      // Serial.println("Failed to parse JSON");
    }
  }