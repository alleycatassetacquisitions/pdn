#pragma once

#include <memory>
#include <string>
#include <iostream>
#include <cstdint>
#include "symbol.hpp"

enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};

class Player {
public:
    Player() = default;
    ~Player() = default;
    
    Player(const std::string& id, Allegiance allegiance, bool isHunter);
    
    std::string toJson() const;

    void fromJson(const std::string &json);

    bool isHunter() const;

    void setIsHunter(bool isHunter);

    void toggleHunter();

    Allegiance getAllegiance() const;

    /** Maps server affiliation display name to artwork enum; defaults to ALLEYCAT. */
    static Allegiance allegianceFromServerName(const std::string& allegianceStr);

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

private:
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
