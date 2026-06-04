#pragma once

#include <cstdint>

// --- GPIO pin assignments ---
constexpr uint8_t primaryButtonPin   = 15;
constexpr uint8_t secondaryButtonPin = 16;
constexpr uint8_t motorPin           = 17;
constexpr uint8_t RXr                = 41;
constexpr uint8_t RXt                = 40;
constexpr uint8_t TXt                = 39; // MIDDLE BAND ON AUDIO CABLE
constexpr uint8_t TXr                = 38; // TIP OF AUDIO CABLE
constexpr uint8_t displayLightsPin   = 13;
constexpr uint8_t gripLightsPin      = 21;
constexpr uint8_t displayCS          = 10;
constexpr uint8_t displayDC          = 9;
constexpr uint8_t displayRST         = 14;

// --- LED strip lengths ---
constexpr uint8_t numDisplayLights = 13;
constexpr uint8_t numGripLights    = 6;

// --- Game timing ---
constexpr unsigned long DUEL_NO_RESULT_TIME = 123456789;

// --- Persistent storage namespace (NVS / Preferences) ---
constexpr const char* PREF_NAMESPACE = "matches";
