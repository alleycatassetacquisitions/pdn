#pragma once

#include <functional>
#include <memory>
#include <string>
#include <iostream>
#include <cstdint>
#include "device/drivers/peer-comms-types.hpp"
#include "symbol.hpp"

enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};

class Player {
public:
    /// Fires after the hunter/bounty role flips.
    using RoleChangedCallback = std::function<void()>;

    Player() = default;
    ~Player() = default;

    Player(const std::string& id, Allegiance allegiance, bool isHunter);

    std::string toJson() const;

    void fromJson(const std::string &json);

    bool isHunter() const;

    void setIsHunter(bool isHunter);

    void toggleHunter();

    /// Registers the role-flip observer (one slot). Fires only on an actual
    /// flip, never on a set that reasserts the current role.
    void setOnRoleChanged(RoleChangedCallback callback);

    /// This player as the packed profile peers receive in the connection
    /// context. userId is 0xFFFF while the player id is not wholly numeric
    /// (registration has not completed); faction and name truncate to the
    /// fixed wire widths.
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

    void setUserID(char *newId);

    /// Reseed libc `rand` from the decimal value of `id` and re-roll the PDN symbol. Call after
    /// the pairing code is entered (PlayerRegistrationState), so gameplay RNG matches the player.
    void applyRngSeedFromUserId();

    std::string getUserID() const;

    void clearUserID();

    unsigned long getLastReactionTime();

    unsigned long getAverageReactionTime();

    int getStreak();

    int getMatchesPlayed();

    int getWins();

    int getLosses();

    Symbol* getSymbol();

    void incrementStreak();

    void resetStreak();

    void incrementMatchesPlayed();

    void incrementWins();

    void incrementLosses();

    void addReactionTime(unsigned long reactionTime);

private:
    // Sole write path for `hunter`, so every flip notifies exactly once no
    // matter which public setter produced it.
    void applyRole(bool isHunter);

    RoleChangedCallback roleChangedCallback;

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
