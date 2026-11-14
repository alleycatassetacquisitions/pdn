#pragma once

#include <cstdint>

// --- GPIO pin assignments ---
// The primary is the small top button, the secondary the big bottom one. Nothing
// else records which is which, and on the waiting screen it is the difference
// between the demo hold and the firmware hold.
constexpr uint8_t primaryButtonPin   = 15; // SMALL TOP BUTTON
constexpr uint8_t secondaryButtonPin = 16; // BIG BOTTOM BUTTON
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

// --- Flash ---
// The match ring's NVS partition: the end of flash, which partitions.csv labels
// fdn_store and a PDN otherwise leaves idle. Deployed tables end before it, so
// there the PDN registers the same range under the same label for each boot;
// renaming it in the table would take a USB flash. Firmware before it kept the
// ring in the default partition.
inline constexpr const char MATCH_RING_PARTITION_LABEL[] = "fdn_store";
constexpr uint32_t MATCH_RING_PARTITION_OFFSET = 0x7E1000;
constexpr uint32_t MATCH_RING_PARTITION_SIZE = 0x1F000;
// The flash every PDN has; the range above ends at its end.
constexpr uint32_t PDN_FLASH_SIZE = 0x800000;
