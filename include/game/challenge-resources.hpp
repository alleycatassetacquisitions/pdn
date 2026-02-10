#pragma once

#include "device/drivers/light-interface.hpp"
#include "device/device-types.hpp"

/*
 * NPC LED colors per game type.
 * These are starting suggestions — will be tuned during playtesting.
 * Each NPC's palette should eventually match the palette players win.
 */
inline LEDColor getNpcLedColor(GameType gameType) {
    switch (gameType) {
        case GameType::GHOST_RUNNER:      return {200, 220, 255};  // Cool white/pale blue
        case GameType::SPIKE_VECTOR:      return {255, 80, 0};     // Red/orange
        case GameType::FIREWALL_DECRYPT:  return {0, 255, 50};     // Green
        case GameType::CIPHER_PATH:       return {150, 0, 255};    // Purple/violet
        case GameType::EXPLOIT_SEQUENCER: return {255, 200, 0};    // Yellow/gold
        case GameType::BREACH_DEFENSE:    return {0, 200, 200};    // Cyan/teal
        case GameType::SIGNAL_ECHO:       return {255, 0, 150};    // Magenta/pink
        default:                          return {128, 128, 128};  // Gray
    }
}
