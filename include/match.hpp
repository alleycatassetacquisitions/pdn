#pragma once

#include <ArduinoJson.h>
#include <UUID.h>

class Match {
public:

    void setupMatch(String id, String hunter, String bounty);

    void setWinner(bool winner_is_hunter);

    String toJson() const;

    void fromJson(const String &json);

    void fillJsonObject(JsonObject &matchObj);

private:
    String match_id;
    String hunter;
    String bounty;
    bool winner_is_hunter; // Indicates if the winner is the hunter
};
