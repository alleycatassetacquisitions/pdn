#include "../include/player.hpp"

#include <ArduinoJson.h>
#include "player.hpp"

Player::Player(const String id0, const Allegiance allegiance0, const bool isHunter0) :
  id(id0),
  allegiance(allegiance0),
  hunter(isHunter0)
{
}

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

void Player::setUserID(UUID &generator)
{
  generator.generate();
  id = generator.toCharArray();
}

String Player::getUserID() const
{
    return id;
}
