#pragma once

#include <string>

using namespace std;

class Match {
public:

    void setupMatch(string id, string hunter, string bounty);

    void setWinner(bool winner_is_hunter);

    string toJson() const;

    void fromJson(const string &json);

private:
    string match_id;
    string hunter;
    string bounty;
    bool winner_is_hunter; // Indicates if the winner is the hunter
};
