#include "game/player.hpp"
#include <memory>
#include <ArduinoJson.h>

Player::Player(const std::string& id, Allegiance allegiance, bool isHunter) :
  id(id),
  allegiance(allegiance),
  hunter(isHunter)
{
}

std::string Player::toJson() const {
    // Create a JSON object for player
    JsonDocument doc;
    JsonObject playerObj = doc.to<JsonObject>();
    playerObj["id"] = id;
    playerObj["name"] = name;
    playerObj["allegiance"] = allegianceStr;
    playerObj["faction"] = faction;
    playerObj["hunter"] = hunter;

    // Serialize the JSON object to a string
    std::string json;
    serializeJson(playerObj, json);
    return json;
}

void Player::fromJson(const std::string &json) {
    // Parse the JSON string into a JSON object
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    // Check if parsing was successful
    if (!error) {
      // Retrieve values from the JSON object
      id = doc["id"].as<std::string>();
      if (doc["name"].is<const char*>()) {
        name = doc["name"].as<std::string>();
      }
      if (doc["allegiance"].is<const char*>()) {
        allegianceStr = doc["allegiance"].as<std::string>();
      }
      if (doc["faction"].is<const char*>()) {
        faction = doc["faction"].as<std::string>();
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
  id = "9998";
}

bool Player::isHunter() const
{
    return hunter;
}

Allegiance Player::getAllegiance() const
{
    return allegiance;
}

void Player::setAllegiance(const std::string& allegianceStr)
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

void Player::setAllegiance(int allegiance)
{
    switch(allegiance) {
        case 0:
            allegianceStr = "None";
            this->allegiance = Allegiance::ALLEYCAT;
            break;
        case 1:
            allegianceStr = "Endline";
            this->allegiance = Allegiance::ENDLINE;
            break;
        case 2:
            allegianceStr = "Helix";
            this->allegiance = Allegiance::HELIX;
            break;
        case 3:
            allegianceStr = "The Resistance";
            this->allegiance = Allegiance::RESISTANCE;
            break;
    }
}

void Player::setAllegiance(Allegiance allegiance)
{
    this->allegiance = allegiance;

    switch(allegiance) {
        case Allegiance::ALLEYCAT:
            allegianceStr = "None";
            break;
        case Allegiance::ENDLINE:
            allegianceStr = "Endline";
            break;
    }
    
}

std::string Player::getAllegianceString() const
{
    return allegianceStr;
}

std::string Player::getName() const
{
    return name;
}

void Player::setName(const std::string& name)
{
    this->name = name;
}

std::string Player::getFaction() const
{
    return faction;
}

void Player::setFaction(const std::string& faction)
{
    this->faction = faction;
}

void Player::setUserID(char* newId)
{
  id = std::string(newId);
}

std::string Player::getUserID() const
{
    return id;
}

void Player::setCurrentMatchId(const std::string& matchId) {
    currentMatchId = matchId;
}

std::string& Player::getCurrentMatchId() {
    return currentMatchId;
}

const std::string& Player::getCurrentMatchId() const {
    return currentMatchId;
}

void Player::setCurrentOpponentId(const std::string& opponentIdParam) {
    currentOpponentId = opponentIdParam;
}

std::string& Player::getCurrentOpponentId() {
    return currentOpponentId;
}

const std::string& Player::getCurrentOpponentId() const {
    return currentOpponentId;
}

void Player::setOpponentMacAddress(const std::string& macAddress) {
    opponentMacAddress = macAddress;
}

std::string& Player::getOpponentMacAddress() {
    return opponentMacAddress;
}

const std::string& Player::getOpponentMacAddress() const {
    return opponentMacAddress;
}

unsigned long Player::getLastReactionTime() const {
    return lastReactionTime;
}

unsigned long Player::getAverageReactionTime() const {
    if(matchesPlayed == 0) {
        return 0;
    }
    return totalReactionTime / matchesPlayed;
}

int Player::getStreak() const {
    return winStreak;
}

int Player::getMatchesPlayed() const {
    return matchesPlayed;
}

int Player::getWins() const {
    return wins;
}

int Player::getLosses() const {
    return losses;
}

void Player::incrementStreak() {
    winStreak++;
}

void Player::resetStreak() {
    winStreak = 0;
}

void Player::incrementMatchesPlayed() {
    matchesPlayed++;
}

void Player::incrementWins() {
    wins++;
}

void Player::incrementLosses() {
    losses++;
}

void Player::addReactionTime(unsigned long reactionTime) {
    lastReactionTime = reactionTime;
    totalReactionTime += reactionTime;
}