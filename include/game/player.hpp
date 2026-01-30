#pragma once

#include <memory>
#include <string>
#include <iostream>

enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};

class Player {
public:
    Player() = default;
    
    Player(const std::string id, const Allegiance allegiance, const bool isHunter);
    
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

    void setCurrentMatchId(std::string matchId);

    std::string *getCurrentMatchId();

    void setCurrentOpponentId(std::string opponentId);
    std::string *getCurrentOpponentId();

    void setOpponentMacAddress(std::string macAddress);

    std::string *getOpponentMacAddress();

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

    std::string* currentMatchId = new std::string();
    std::string* currentOpponentId = new std::string();
    std::string* opponentMacAddress = new std::string();
    
    bool hunter = true;
};
