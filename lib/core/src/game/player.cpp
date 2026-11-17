#include "game/player.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"

static const char TAG[] = "Player";

/** These methods in this namespace are a compatibility layer only until we replace all the old player apis. */
namespace {

bool equalsIgnoreCase(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        if (std::tolower(static_cast<unsigned char>(*left)) !=
            std::tolower(static_cast<unsigned char>(*right))) {
            return false;
        }
        left++;
        right++;
    }
    return *left == *right;
}

bool containsIgnoreCase(const char* haystack, const char* needle) {
    if (haystack == nullptr || needle == nullptr || needle[0] == '\0') {
        return false;
    }
    for (const char* start = haystack; *start != '\0'; start++) {
        const char* haystackCursor = start;
        const char* needleCursor = needle;
        while (*needleCursor != '\0' && std::tolower(static_cast<unsigned char>(*haystackCursor)) ==
                                        std::tolower(static_cast<unsigned char>(*needleCursor))) {
            haystackCursor++;
            needleCursor++;
        }
        if (*needleCursor == '\0') {
            return true;
        }
    }
    return false;
}

bool teamNameIsHunter(const char* teamName) { return equalsIgnoreCase(teamName, "Hunter"); }

bool teamNameIsBounty(const char* teamName) { return equalsIgnoreCase(teamName, "Bounty"); }

Allegiance allegianceFromAffiliationName(const char* affiliationName) {
    if (affiliationName == nullptr || affiliationName[0] == '\0') {
        return Allegiance::ALLEYCAT;
    }
    if (containsIgnoreCase(affiliationName, "Endline")) {
        return Allegiance::ENDLINE;
    }
    if (containsIgnoreCase(affiliationName, "Helix")) {
        return Allegiance::HELIX;
    }
    if (containsIgnoreCase(affiliationName, "Resistance")) {
        return Allegiance::RESISTANCE;
    }
    return Allegiance::ALLEYCAT;
}

}  // namespace

Player::Player(const std::string& id, Allegiance allegiance, bool isHunter)
    : id(id)
    , allegiance(allegiance)
    , hunter(isHunter) {}

void Player::toggleHunter() { hunter = !hunter; }

void Player::setIsHunter(bool isHunter) { hunter = isHunter; }

void Player::clearUserID() {
    id = "9998";
    symbol.updateFromUserIdString(id);
}

bool Player::isHunter() const { return hunter; }

Allegiance Player::getAllegiance() const { return allegiance; }

void Player::setAllegiance(const std::string& allegianceStr) {
    this->allegianceStr = allegianceStr;
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

void Player::applyRngSeedFromDecimalString(const char* decimalString) {
    if (decimalString == nullptr || decimalString[0] == '\0') {
        return;
    }
    const unsigned int seed =
        static_cast<unsigned int>(std::strtoul(decimalString, nullptr, 10));
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

bool Player::hasRegistrationProfile() const { return registrationProfileStored; }

const UUIDBytes& Player::getServerPlayerId() const { return serverPlayerId; }

const char* Player::getServerPlayerName() const { return serverPlayerName; }

const Team& Player::getRegistrationTeam() const { return registrationTeam; }

const std::vector<Affiliation>& Player::getRegistrationAffiliations() const {
    return registrationAffiliations;
}

bool Player::hasTeam() const { return !registrationTeam.id.isZero(); }

void Player::applyRegistrationProfile(const UUIDBytes& serverId, const char* serverName,
                                      const Team& team,
                                      const std::vector<Affiliation>& affiliations) {
    serverPlayerId = serverId;
    if (serverName != nullptr) {
        std::strncpy(serverPlayerName, serverName, PlayerLimits::NAME_BUFFER_SIZE - 1);
        serverPlayerName[PlayerLimits::NAME_BUFFER_SIZE - 1] = '\0';
    } else {
        serverPlayerName[0] = '\0';
    }
    registrationTeam = team;
    registrationAffiliations = affiliations;
    registrationProfileStored = true;
    syncLegacyFieldsFromRegistration();
}

void Player::clearRegistrationProfile() {
    serverPlayerId = UUIDBytes();
    serverPlayerName[0] = '\0';
    registrationTeam = Team();
    registrationAffiliations.clear();
    registrationProfileStored = false;
}

void Player::syncLegacyFieldsFromRegistration() {
    name = serverPlayerName;

    char uuidText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
    if (!serverPlayerId.isZero()) {
        serverPlayerId.format(uuidText);
        id = uuidText;
        symbol.updateFromUserIdString(id);
    }

    if (teamNameIsHunter(registrationTeam.name)) {
        hunter = true;
    } else if (teamNameIsBounty(registrationTeam.name)) {
        hunter = false;
    } else if (hasTeam()) {
        hunter = true;
        LOG_W(TAG, "Unknown team name '%s'; defaulting to hunter", registrationTeam.name);
    }

    if (!registrationAffiliations.empty()) {
        const Affiliation& primary = registrationAffiliations.front();
        setAllegiance(allegianceFromAffiliationName(primary.affiliationName));
        faction = primary.affiliationName;
    }
}