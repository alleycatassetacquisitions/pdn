#pragma once

#include <string>

using namespace std;

enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};

class Player {
public:
    string toJson() const;

    void fromJson(const string &json);

    bool isHunter() const;

    void toggleHunter();

    Allegiance getAllegiance() const;

    void setUserID(char *newId);

    string getUserID() const;

    void clearUserID();

    void setCurrentMatchId(string matchId);

    string *getCurrentMatchId();

    void setCurrentOpponentId(string opponentId);

    string *getCurrentOpponentId();

private:
    string id = "default";

    Allegiance allegiance = Allegiance::RESISTANCE;

    string *currentMatchId = nullptr;
    string *currentOpponentId = nullptr;

    bool hunter = false;
};
