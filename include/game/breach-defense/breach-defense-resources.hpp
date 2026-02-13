#pragma once

#include "device/drivers/light-interface.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include <cstdint>

/*
 * Breach Defense LED palettes — red/crimson "defensive shield" theme.
 */

// Primary palette (4 colors) — used for non-idle animations
const LEDColor breachDefenseColors[4] = {
    LEDColor(220, 20, 20),    // Crimson
    LEDColor(180, 0, 0),      // Dark red
    LEDColor(255, 60, 30),    // Orange-red
    LEDColor(150, 0, 20),     // Deep crimson
};

// Idle palette (8 colors) — used for idle breathing animation
const LEDColor breachDefenseIdleColors[8] = {
    LEDColor(220, 20, 20),    LEDColor(180, 0, 0),
    LEDColor(255, 60, 30),    LEDColor(150, 0, 20),
    LEDColor(220, 20, 20),    LEDColor(180, 0, 0),
    LEDColor(255, 60, 30),    LEDColor(150, 0, 20),
};

// Idle state — crimson breathing pulse
const LEDState BREACH_DEFENSE_IDLE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(
            breachDefenseIdleColors[i % 8], 65);
        state.rightLights[i] = LEDState::SingleLEDState(
            breachDefenseIdleColors[i % 8], 65);
    }
    return state;
}();

// Gameplay state — chase animation base colors
const LEDState BREACH_DEFENSE_GAMEPLAY_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(100 + (i * 15));
        state.leftLights[i] = LEDState::SingleLEDState(
            breachDefenseColors[i % 4], brightness);
        state.rightLights[i] = LEDState::SingleLEDState(
            breachDefenseColors[i % 4], brightness);
    }
    return state;
}();

// Win state — bright green sweep
const LEDState BREACH_DEFENSE_WIN_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t g = static_cast<uint8_t>(255 - (i * 15));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 50), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 50), 255);
    }
    return state;
}();

// Lose state — dark red fade
const LEDState BREACH_DEFENSE_LOSE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(255 - (i * 25));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
    }
    return state;
}();

// Block state — green flash for successful block
const LEDState BREACH_DEFENSE_BLOCK_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
    }
    return state;
}();

// Breach state — red flash for missed threat
const LEDState BREACH_DEFENSE_BREACH_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
    }
    return state;
}();
