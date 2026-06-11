#pragma once

#include <cstdint>
#include <string>
#include <cstddef>
#include "game/player.hpp"

// --- GPIO pin assignments ---
constexpr uint8_t fdnPrimaryButtonPin    = 15;
constexpr uint8_t fdnSecondaryButtonPin  = 16;
constexpr uint8_t fdnTertiaryButtonPin   = 6;
constexpr uint8_t fdnMotorPin            = 17;

// Primary input jack (Serial2: RXr=41, RXt=40)
constexpr uint8_t fdnRXr                 = 41;
constexpr uint8_t fdnRXt                 = 40;

// Secondary input jack (Serial1: RXr2=39, RXt2=38)
constexpr uint8_t fdnRXr2               = 39;
constexpr uint8_t fdnRXt2               = 38;

// LED strips
constexpr uint8_t fdnRecessLightsPin    = 21;
constexpr uint8_t fdnFinLightsPin       = 13;
constexpr uint8_t fdnNumRecessLights    = 22;
constexpr uint8_t fdnNumFinLights       = 9;

// SSD1309 display
constexpr uint8_t fdnDisplayCS          = 10;
constexpr uint8_t fdnDisplayDC          = 9;
constexpr uint8_t fdnDisplayRST         = 14;

// --- Persistent storage namespace ---
constexpr const char* FDN_PREF_NAMESPACE = "fdn_hacks";

// --- RSSI thresholds for player proximity detection ---
constexpr int FDN_STRONG_RSSI_THRESHOLD = -30;
constexpr int FDN_MEDIUM_RSSI_THRESHOLD = -50;

// --- FDN device identity ---
// Player ID is 4 characters, zero-padded (e.g. FDN box 0 = "0000")
constexpr uint8_t FDN_BOX_ID = 0;
constexpr size_t  FDN_PLAYER_ID_LENGTH = 4;

inline const std::string FDN_PLAYER_ID =
    std::string(FDN_PLAYER_ID_LENGTH - std::to_string(FDN_BOX_ID).length(), '0') +
    std::to_string(FDN_BOX_ID);

inline Player FDN_PLAYER = Player(FDN_PLAYER_ID, Allegiance::ALLEYCAT, false);
