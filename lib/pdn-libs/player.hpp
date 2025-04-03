#pragma once

#include <memory>
#include <string>
#include <iostream>

using namespace std;

enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};

class Player {
public:
    Player() = default;
    
    Player(const string id0, const Allegiance allegiance0, const bool isHunter0);
    
    string toJson() const;

    void fromJson(const string &json);

    bool isHunter() const;

    void setIsHunter(bool isHunter);

    void toggleHunter();

    Allegiance getAllegiance() const;

    void setAllegiance(const string& allegianceStr);

    void setAllegiance(const Allegiance allegiance);

    string getAllegianceString() const;

    string getName() const;

    void setName(const string& name);

    string getFaction() const;

    void setFaction(const string& faction);

    void setUserID(char *newId);

    string getUserID() const;

    void clearUserID();

    void setCurrentMatchId(string matchId);

    string *getCurrentMatchId();

    void setCurrentOpponentId(string opponentId);
    string *getCurrentOpponentId();

    void setOpponentMacAddress(string macAddress);

    string *getOpponentMacAddress();

private:
    string id = "default";
    string name = "";
    string allegianceStr = "none";
    string faction = "";

    Allegiance allegiance = Allegiance::RESISTANCE;

    string* currentMatchId = new string();
    string* currentOpponentId = new string();
    string* opponentMacAddress = new string();
    
    bool hunter = true;
};
