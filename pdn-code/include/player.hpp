#pragma once

#include <Arduino.h>
#include <UUID.h>


enum class Allegiance {
  ALLEYCAT = 0,
  ENDLINE = 1,
  HELIX = 2,
  RESISTANCE = 3
};

class Player {
public:
  String toJson() const;

  void fromJson(const String &json);

  bool isHunter() const;
  void toggleHunter();

  Allegiance getAllegiance() const;
  
  void setUserID(char* newId);
  String getUserID() const;
  void clearUserID();

  void setCurrentMatchId(String* matchId);
  String getCurrentMatchId();

  void setCurrentOpponentId(String* opponentId);
  String getCurrentOpponentId();

private:
  String id = "default";

  Allegiance allegiance = Allegiance::RESISTANCE;

  String currentMatchId = "";
  String currentOpponentId = "";

  bool hunter = false;
};
