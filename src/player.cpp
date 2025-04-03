#include "player.hpp"
#include <memory>
#include <ArduinoJson.h>

Player::Player(const string id0, const Allegiance allegiance0, const bool isHunter0) :
  id(id0),
  allegiance(allegiance0),
  hunter(isHunter0)
{
}

string Player::toJson() const {
    // Create a JSON object for player
    StaticJsonDocument<256> doc;
    JsonObject playerObj = doc.to<JsonObject>();
    playerObj["id"] = id;
    playerObj["name"] = name;
    playerObj["allegiance"] = allegianceStr;
    playerObj["faction"] = faction;
    playerObj["hunter"] = hunter;

    // Serialize the JSON object to a string
    string json;
    serializeJson(playerObj, json);
    return json;
}

void Player::fromJson(const string &json) {
    // Parse the JSON string into a JSON object
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, json);

    // Check if parsing was successful
    if (!error) {
      // Retrieve values from the JSON object
      id = doc["id"].as<string>();
      if (doc.containsKey("name")) {
        name = doc["name"].as<string>();
      }
      if (doc.containsKey("allegiance")) {
        allegianceStr = doc["allegiance"].as<string>();
      }
      if (doc.containsKey("faction")) {
        faction = doc["faction"].as<string>();
      }
      hunter = doc["hunter"];
    } else {
      // Serial.println("Failed to parse JSON");
    }
}

void Player::toggleHunter()
{
  hunter = !hunter;
}

void Player::setIsHunter(bool isHunter) 
{
  hunter = isHunter;
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

void Player::setAllegiance(const string& allegianceStr)
{
    this->allegianceStr = allegianceStr;
    
    // Set the enum value based on the string
    if (allegianceStr == "None") {
        allegiance = Allegiance::ALLEYCAT;
    } else if (allegianceStr == "Endline") {
        allegiance = Allegiance::ENDLINE;
    } else if (allegianceStr == "Helix") {
        allegiance = Allegiance::HELIX;
    } else if (allegianceStr == "The Resistance") {
        allegiance = Allegiance::RESISTANCE;
    }
}

void Player::setAllegiance(const Allegiance allegiance)
{
    this->allegiance = allegiance;
    switch(allegiance) {
        case Allegiance::ALLEYCAT:
            allegianceStr = "None";
            break;
        case Allegiance::ENDLINE:
            allegianceStr = "Endline";
            break;
        case Allegiance::HELIX:
            allegianceStr = "Helix";
            break;
        case Allegiance::RESISTANCE:
            allegianceStr = "The Resistance";
    }
}

string Player::getAllegianceString() const
{
    return allegianceStr;
}

string Player::getName() const
{
    return name;
}

void Player::setName(const string& name)
{
    this->name = name;
}

string Player::getFaction() const
{
    return faction;
}

void Player::setFaction(const string& faction)
{
    this->faction = faction;
}

void Player::setUserID(char* newId)
{
  id = string(newId);
}

string Player::getUserID() const
{
    return id;
}

void Player::setCurrentMatchId(string matchId) {
    *currentMatchId = matchId;
}

string* Player::getCurrentMatchId() {
    return currentMatchId;
}

void Player::setCurrentOpponentId(string opponentId) {
    *currentOpponentId = opponentId;
}

string* Player::getCurrentOpponentId() {
    return currentOpponentId;
}

void Player::setOpponentMacAddress(string macAddress) {
    *opponentMacAddress = macAddress;
}

string* Player::getOpponentMacAddress() {
    return opponentMacAddress;
}