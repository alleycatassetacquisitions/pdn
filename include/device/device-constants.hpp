#pragma once

// Serial framing and protocol constants have moved to lib/core/include/protocol-constants.hpp.
// Haptics range constants are in lib/core/include/device/drivers/haptics.hpp.
// Light brightness constants are in lib/core/include/device/drivers/light-interface.hpp.
// ESP-NOW channel has moved to lib/esp32-drivers/include/esp32-driver-constants.hpp.
// Remaining PDN-specific constants (pins, LED counts, storage keys) will move to
// src/pdn/pdn-constants.hpp in the next migration step.

#include <cstdint>
#include <string>
#include "protocol-constants.hpp"

constexpr uint8_t primaryButtonPin = 15;
constexpr uint8_t secondaryButtonPin = 16;
constexpr uint8_t motorPin = 17;
constexpr uint8_t RXr = 41;
constexpr uint8_t RXt = 40;
constexpr uint8_t TXt = 39; //MIDDLE BAND ON AUDIO CABLE
constexpr uint8_t TXr = 38; //TIP OF AUDIO CABLE
constexpr uint8_t displayLightsPin = 13;
constexpr uint8_t gripLightsPin = 21;
constexpr uint8_t displayCS = 10;
constexpr uint8_t displayDC = 9;
constexpr uint8_t displayRST = 14;

constexpr uint8_t numDisplayLights = 13;
constexpr uint8_t numGripLights = 6;

constexpr unsigned long DUEL_NO_RESULT_TIME = 123456789;

constexpr const char* PREF_NAMESPACE = "matches";
constexpr const char* PREF_COUNT_KEY = "count";
constexpr const char* PREF_MATCH_KEY = "match_";
constexpr uint8_t MAX_MATCHES = 255;
