#include "../include/player.hpp"

#include <ArduinoJson.h>
#include "player.hpp"


String Player::toJson() const {
    // Create a JSON object for player
    StaticJsonDocument<128> doc;
    JsonObject playerObj = doc.to<JsonObject>();
    playerObj["id"] = id;
    playerObj["allegiance"] = (int)allegiance;
    playerObj["hunter"] = hunter;

    // Serialize the JSON object to a string
    String json;
    serializeJson(playerObj, json);
    return json;
  }

void Player::fromJson(const String &json) {
    // Parse the JSON string into a JSON object
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, json);

    // Check if parsing was successful
    if (!error) {
      // Retrieve values from the JSON object
      id = doc["id"].as<String>();
      allegiance = (Allegiance)(int)doc["allegiance"];
      hunter = doc["hunter"];
    } else {
      // Serial.println("Failed to parse JSON");
    }
}

void Player::toggleHunter()
{
  hunter = !hunter;
}

void Player::clearUserID()
{
  id = "default";
}

bool Player::isHunter() const
{
    return hunter;
}

Allegiance Player::getAllegiance() const
{
    return allegiance;
}

void Player::setUserID(char* newId)
{
  id = newId;
}

String Player::getUserID() const
{
    return id;
}

void Player::setCurrentMatchId(String matchId) {
    *currentMatchId = matchId;
}

String* Player::getCurrentMatchId() {
    return currentMatchId;
}

void Player::setCurrentOpponentId(String opponentId) {
    *currentOpponentId = opponentId;
}

String* Player::getCurrentOpponentId() {
    return currentOpponentId;
}
