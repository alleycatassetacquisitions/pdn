#include "game/player.hpp"
#include <memory>
#include <cstring>
#include <cstdlib>
#include <ArduinoJson.h>
#include "wireless/mac-functions.hpp"

Player::Player(const std::string& id, Allegiance allegiance, bool isHunter)
    : id(id)
    , allegiance(allegiance)
    , hunter(isHunter) {}

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

void Player::fromJson(const std::string& json) {
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

void Player::toggleHunter() { hunter = !hunter; }

void Player::setIsHunter(bool isHunter) { hunter = isHunter; }

void Player::clearUserID() {
    id = "9998";
    symbol.updateFromUserIdString(id);
}

bool Player::isHunter() const { return hunter; }

Allegiance Player::getAllegiance() const { return allegiance; }

Allegiance Player::allegianceFromServerName(const std::string& allegianceStr) {
    if (allegianceStr == "Endline") {
        return Allegiance::ENDLINE;
    }
    if (allegianceStr == "Helix") {
        return Allegiance::HELIX;
    }
    if (allegianceStr == "The Resistance") {
        return Allegiance::RESISTANCE;
    }
    return Allegiance::ALLEYCAT;
}

void Player::setAllegiance(const std::string& allegianceStr) {
    this->allegianceStr = allegianceStr;
    allegiance = allegianceFromServerName(allegianceStr);
}

void Player::setAllegiance(int allegiance) {
    switch (allegiance) {
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

void Player::setAllegiance(Allegiance allegiance) {
    this->allegiance = allegiance;

    switch (allegiance) {
        case Allegiance::ALLEYCAT: allegianceStr = "None"; break;
        case Allegiance::ENDLINE: allegianceStr = "Endline"; break;
        case Allegiance::HELIX: allegianceStr = "Helix"; break;
        case Allegiance::RESISTANCE: allegianceStr = "The Resistance"; break;
    }
}

std::string Player::getAllegianceString() const { return allegianceStr; }

std::string Player::getName() const { return name; }

void Player::setName(const std::string& name) { this->name = name; }

std::string Player::getFaction() const { return faction; }

void Player::setFaction(const std::string& faction) { this->faction = faction; }

void Player::setUserID(char* newId) {
    id = std::string(newId);
    symbol.updateFromUserIdString(id);
}

void Player::applyRngSeedFromUserId() {
    const unsigned int seed = static_cast<unsigned int>(std::strtoul(id.c_str(), nullptr, 10));
    std::srand(seed);
    symbol.setRandomSymbol();
}

std::string Player::getUserID() const { return id; }

unsigned long Player::getLastReactionTime() const { return lastReactionTime; }

unsigned long Player::getAverageReactionTime() const {
    if (matchesPlayed == 0) {
        return 0;
    }
    return totalReactionTime / matchesPlayed;
}

int Player::getStreak() const { return winStreak; }

int Player::getMatchesPlayed() const { return matchesPlayed; }

int Player::getWins() const { return wins; }

int Player::getLosses() const { return losses; }

Symbol* Player::getSymbol() { return &symbol; }

void Player::incrementStreak() { winStreak++; }

void Player::resetStreak() { winStreak = 0; }

void Player::incrementMatchesPlayed() { matchesPlayed++; }

void Player::incrementWins() { wins++; }

void Player::incrementLosses() { losses++; }

void Player::addReactionTime(unsigned long reactionTime) {
    lastReactionTime = reactionTime;
    totalReactionTime += reactionTime;
}