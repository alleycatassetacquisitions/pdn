#include "game/player.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

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
    if (containsIgnoreCase(affiliationName, "Resistance") ||
        containsIgnoreCase(affiliationName, "Reboot")) {
        return Allegiance::RESISTANCE;
    }
    return Allegiance::ALLEYCAT;
}

}  // namespace

Player::Player(const std::string& id, Allegiance allegiance, bool isHunter)
    : id(id)
    , allegiance(allegiance)
    , hunter(isHunter) {}

void Player::toggleHunter() { applyRole(!hunter); }

void Player::setIsHunter(bool isHunter) { applyRole(isHunter); }

void Player::applyRole(bool isHunter) {
    if (hunter == isHunter) return;
    hunter = isHunter;
    if (roleChangedCallback) roleChangedCallback();
}

void Player::setOnRoleChanged(RoleChangedCallback callback) {
    roleChangedCallback = std::move(callback);
}

PlayerProfile Player::toProfile() const {
    PlayerProfile profile{};
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(id.c_str(), &end, 10);
    // Only a numeric id below the sentinel fits the wire field; anything else
    // keeps the profile's default, and `registered` below is what tells peers a
    // profile has been applied.
    if (!id.empty() && end != nullptr && *end == '\0' && parsed < PEER_USER_ID_NONE) {
        profile.userId = static_cast<uint16_t>(parsed);
    }
    profile.registered = registrationProfileStored ? 1 : 0;
    profile.gameRole = hunter ? 1 : 0;
    profile.allegiance = static_cast<uint8_t>(allegiance);
    // snprintf both truncates to the fixed wire width and NUL-terminates.
    std::snprintf(profile.faction, sizeof(profile.faction), "%s", faction.c_str());
    std::snprintf(profile.name, sizeof(profile.name), "%s", name.c_str());
    return profile;
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

const PlayerTotals& Player::getRegistrationTotals() const { return registrationTotals; }

void Player::applyRegistrationProfile(const UUIDBytes& serverId, const char* serverName,
                                      const Team& team,
                                      const std::vector<Affiliation>& affiliations,
                                      const PlayerTotals& totals) {
    serverPlayerId = serverId;
    if (serverName != nullptr) {
        std::strncpy(serverPlayerName, serverName, PlayerLimits::NAME_BUFFER_SIZE - 1);
        serverPlayerName[PlayerLimits::NAME_BUFFER_SIZE - 1] = '\0';
    } else {
        serverPlayerName[0] = '\0';
    }
    registrationTeam = team;
    registrationAffiliations = affiliations;
    registrationTotals = totals;
    // A reader taking losses as matches less wins must not wrap.
    if (!registrationTotals.isConsistent()) {
        LOG_W(TAG, "Totals dropped: %lu wins in %lu matches",
              static_cast<unsigned long>(totals.wins),
              static_cast<unsigned long>(totals.matchesPlayed));
        registrationTotals = PlayerTotals{};
    }
    registrationProfileStored = true;
    syncLegacyFieldsFromRegistration();
}

void Player::clearRegistrationProfile() {
    serverPlayerId = UUIDBytes();
    serverPlayerName[0] = '\0';
    registrationTeam = Team();
    registrationAffiliations.clear();
    registrationTotals = PlayerTotals{};
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

    const bool hunterTeam = teamNameIsHunter(registrationTeam.name);
    if (!hunterTeam && !teamNameIsBounty(registrationTeam.name) && hasTeam()) {
        LOG_W(TAG, "Unknown team name '%s'; defaulting to bounty", registrationTeam.name);
    }
    hunter = hunterTeam;

    Allegiance resolvedAllegiance = Allegiance::ALLEYCAT;
    for (const Affiliation& affiliation : registrationAffiliations) {
        resolvedAllegiance = allegianceFromAffiliationName(affiliation.affiliationName);
        if (resolvedAllegiance != Allegiance::ALLEYCAT) break;
    }
    setAllegiance(resolvedAllegiance);
    faction =
    registrationAffiliations.empty() ? "" : registrationAffiliations.front().affiliationName;

    // A profile changes what peers are told even when the role stands, and the
    // observer sends the profile as it finds it, so it fires once and last.
    if (roleChangedCallback) roleChangedCallback();
}