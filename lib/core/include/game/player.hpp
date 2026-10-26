#pragma once

#include "game/player-types.hpp"
#include "symbol.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};

/** Gameplay session object: server registration fields, legacy allegiance/hunter, stats, and
 * symbol. */
class Player {
public:
    Player() = default;
    ~Player() = default;

    Player(const std::string& id, Allegiance allegiance, bool isHunter);

    bool isHunter() const;

    void setIsHunter(bool isHunter);

    void toggleHunter();

    Allegiance getAllegiance() const;

    void setAllegiance(const std::string& allegianceStr);

    void setAllegiance(int allegiance);

    void setAllegiance(Allegiance allegiance);

    std::string getAllegianceString() const;

    std::string getName() const;

    void setName(const std::string& name);

    std::string getFaction() const;

    void setFaction(const std::string& faction);

    /** Soon to be deprecated */
    void setUserID(char* newId);

    /** Reseed libc `rand` from a decimal string (e.g. registration PIN) and re-roll the symbol. */
    void applyRngSeedFromDecimalString(const char* decimalString);

    std::string getUserID() const;

    void clearUserID();

    /** @return Milliseconds for the most recent duel reaction sample. */
    unsigned long getLastReactionTime() const;

    /** @return Mean reaction time across recorded matches, or 0 when none played. */
    unsigned long getAverageReactionTime() const;

    /** @return Current consecutive win count. */
    int getStreak() const;

    /** @return Total matches recorded for this session profile. */
    int getMatchesPlayed() const;

    /** @return Win count for this session profile. */
    int getWins() const;

    /** @return Loss count for this session profile. */
    int getLosses() const;

    Symbol* getSymbol();

    void incrementStreak();

    void resetStreak();

    void incrementMatchesPlayed();

    void incrementWins();

    void incrementLosses();

    void addReactionTime(unsigned long reactionTime);

    /** @return true when a server registration profile is stored. */
    bool hasRegistrationProfile() const;

    /** @return Server player id from the active event registration. */
    const UUIDBytes& getServerPlayerId() const;

    /** @return Display name from registration (fixed buffer; empty when none). */
    const char* getServerPlayerName() const;

    /** @return Team assignment from registration. */
    const Team& getRegistrationTeam() const;

    /** @return Category affiliations from registration. */
    const std::vector<Affiliation>& getRegistrationAffiliations() const;

    /** @return true when registration includes a non-nil team id. */
    bool hasTeam() const;

    /**
     * Replaces registration fields and syncs legacy gameplay fields. Used by the device player
     * decoder and demo registration paths.
     */
    void applyRegistrationProfile(const UUIDBytes& serverId, const char* serverName,
                                  const Team& team, const std::vector<Affiliation>& affiliations);

    /** Clears registration fields only; does not reset duel stats or legacy fields. */
    void clearRegistrationProfile();

private:
    void syncLegacyFieldsFromRegistration();

    UUIDBytes serverPlayerId;
    char serverPlayerName[PlayerLimits::NAME_BUFFER_SIZE] = {};
    Team registrationTeam;
    std::vector<Affiliation> registrationAffiliations;
    bool registrationProfileStored = false;

    std::string id = "default";
    std::string name = "";
    std::string allegianceStr = "none";
    std::string faction = "";

    int winStreak = 0;

    int matchesPlayed = 0;

    int wins = 0;

    int losses = 0;

    unsigned long lastReactionTime = 0;

    unsigned long totalReactionTime = 0;

    Allegiance allegiance = Allegiance::RESISTANCE;

    Symbol symbol;

    bool hunter = true;
};
