#pragma once

#include <cstdint>
#include <string>
#include "game/player.hpp"

constexpr uint8_t FDN_BOX_ID = 0;

constexpr int STRONG_RSSI_THRESHOLD = -30;
constexpr int MEDIUM_RSSI_THRESHOLD = -50;

inline const std::string PLAYER_ID = std::string(PLAYER_ID_LENGTH - std::to_string(FDN_BOX_ID).length(), '0') + std::to_string(FDN_BOX_ID);
inline Player FDN_PLAYER = Player(PLAYER_ID, Allegiance::ALLEYCAT, false);
