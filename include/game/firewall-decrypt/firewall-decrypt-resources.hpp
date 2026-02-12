#pragma once

#include "device/drivers/light-interface.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"

/*
 * Firewall Decrypt LED palettes — green/cyan "hacker" theme.
 */

// Idle palette (8 colors) — used for NPC idle animation
const LEDColor firewallDecryptIdleColors[8] = {
    LEDColor(0, 255, 100),   LEDColor(0, 200, 150),
    LEDColor(0, 255, 200),   LEDColor(0, 150, 255),
    LEDColor(0, 255, 100),   LEDColor(0, 200, 150),
    LEDColor(0, 255, 200),   LEDColor(0, 150, 255),
};

const LEDState FIREWALL_DECRYPT_IDLE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(
            firewallDecryptIdleColors[i % 8], 65);
        state.rightLights[i] = LEDState::SingleLEDState(
            firewallDecryptIdleColors[i % 8], 65);
    }
    return state;
}();

const LEDState FIREWALL_DECRYPT_WIN_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t g = static_cast<uint8_t>(255 - (i * 15));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 100), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 100), 255);
    }
    return state;
}();

const LEDState FIREWALL_DECRYPT_LOSE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(255 - (i * 25));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
    }
    return state;
}();

const LEDState FIREWALL_DECRYPT_CORRECT_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
    }
    return state;
}();

// Difficulty presets
inline FirewallDecryptConfig makeDecryptEasyConfig() {
    FirewallDecryptConfig c;
    c.numCandidates = 5;
    c.numRounds = 3;
    c.similarity = 0.2f;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

inline FirewallDecryptConfig makeDecryptHardConfig() {
    FirewallDecryptConfig c;
    c.numCandidates = 10;
    c.numRounds = 4;
    c.similarity = 0.7f;
    c.timeLimitMs = 15000;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

const FirewallDecryptConfig FIREWALL_DECRYPT_EASY = makeDecryptEasyConfig();
const FirewallDecryptConfig FIREWALL_DECRYPT_HARD = makeDecryptHardConfig();
