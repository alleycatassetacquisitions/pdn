#pragma once

#include <memory>
#include <string>
#include <iostream>
#include <cstdint>

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

    void setAllegiance(const std::string& allegianceStr);

    void setAllegiance(int allegiance);

    void setAllegiance(Allegiance allegiance);

    std::string getAllegianceString() const;

    std::string getName() const;

    void setName(const std::string& name);

    std::string getFaction() const;

    void setFaction(const std::string& faction);

    void setUserID(char *newId);

    std::string getUserID() const;

    void clearUserID();

    void setCurrentMatchId(const std::string& matchId);

    std::string& getCurrentMatchId();
    const std::string& getCurrentMatchId() const;

    void setCurrentOpponentId(const std::string& opponentId);
    std::string& getCurrentOpponentId();
    const std::string& getCurrentOpponentId() const;

    void setOpponentMacAddress(const std::string& macAddress);
    void setOpponentMacAddress(const uint8_t* macBytes);

    const uint8_t* getOpponentMacBytes() const;
    bool hasOpponentMac() const;

    unsigned long getLastReactionTime();

    unsigned long getAverageReactionTime();

    int getStreak();

    int getMatchesPlayed();

    int getWins();

    int getLosses();

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

    std::string currentMatchId;
    std::string currentOpponentId;
    uint8_t opponentMacBytes[6] = {};
    bool opponentMacValid = false;
    
    bool hunter = true;
};
