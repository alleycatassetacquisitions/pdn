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
  
  void setUserID(UUID& generator);
  String getUserID() const;
  void clearUserID();

private:
  String id = "default";

  Allegiance allegiance = Allegiance::RESISTANCE;

  bool hunter = false;
};
