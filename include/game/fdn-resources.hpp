#pragma once

#include "device/drivers/light-interface.hpp"
#include "device/device-types.hpp"

/*
 * NPC LED colors per game type.
 */
inline LEDColor getNpcLedColor(GameType gameType) {
    switch (gameType) {
        case GameType::GHOST_RUNNER:      return {200, 220, 255};
        case GameType::SPIKE_VECTOR:      return {255, 80, 0};
        case GameType::FIREWALL_DECRYPT:  return {0, 255, 50};
        case GameType::CIPHER_PATH:       return {150, 0, 255};
        case GameType::EXPLOIT_SEQUENCER: return {255, 200, 0};
        case GameType::BREACH_DEFENSE:    return {0, 200, 200};
        case GameType::SIGNAL_ECHO:       return {255, 0, 150};
        default:                          return {128, 128, 128};
    }
}
