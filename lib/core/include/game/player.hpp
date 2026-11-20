#pragma once

#include "device/drivers/peer-comms-types.hpp"
#include "game/player-types.hpp"
#include "symbol.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
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
    /** Fires on a role flip and on a registration profile applied or cleared (despite its name). */
    using RoleChangedCallback = std::function<void()>;

    Player() = default;
    ~Player() = default;

    Player(const std::string& id, Allegiance allegiance, bool isHunter);

    bool isHunter() const;

    void setIsHunter(bool isHunter);

    void toggleHunter();

    /**
     * Registers the observer (one slot): fires on a hunter/bounty flip (not on a
     * set that reasserts it), on every registration profile applied, and on a
     * clear while a profile is held.
     */
    void setOnRoleChanged(RoleChangedCallback callback);

    /**
     * This player as the packed profile peers receive in the connection
     * context. userId is 0xFFFF while the player id is not wholly numeric
     * (registration has not completed); faction and name truncate to the
     * fixed wire widths.
     */
    PlayerProfile toProfile() const;

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

    /**
     * Reseeds libc `rand` from the server player UUID, the same on any PDN for
     * one player, and re-rolls the symbol.
     */
    void applyRngSeedFromServerPlayerId();

    std::string getUserID() const;

    void clearUserID();

    /** @return Milliseconds for the most recent match reaction sample. */
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
     * Replaces registration fields and syncs legacy gameplay fields, then fires
     * the role-changed observer once.
     */
    void applyRegistrationProfile(const UUIDBytes& serverId, const char* serverName,
                                  const Team& team, const std::vector<Affiliation>& affiliations);

    /**
     * Clears registration fields only; does not reset match stats or legacy
     * fields. Fires the role-changed observer when a profile was held.
     */
    void clearRegistrationProfile();

    /** Test-build id: @p mac reversed, then a `7e57` tail; version nibble 0, never a server id. */
    static UUIDBytes testPlayerIdFromMac(const uint8_t* mac);

private:
    // Write path for `hunter` from the setters, so every flip notifies exactly
    // once; a registration profile sets it directly and notifies once for all.
    void applyRole(bool isHunter);
    void syncLegacyFieldsFromRegistration();

    RoleChangedCallback roleChangedCallback;

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
