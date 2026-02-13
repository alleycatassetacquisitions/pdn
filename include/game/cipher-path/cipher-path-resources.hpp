#pragma once

#include "device/drivers/light-interface.hpp"
#include <cstdint>

/*
 * Cipher Path LED palettes — green/emerald "pathfinding" theme.
 */

// Primary palette (4 colors)
const LEDColor cipherPathColors[4] = {
    LEDColor(0, 255, 128),   // Emerald green
    LEDColor(0, 200, 100),   // Dark emerald
    LEDColor(50, 255, 150),  // Light emerald
    LEDColor(0, 180, 80),    // Forest green
};

// Idle palette (8 colors)
const LEDColor cipherPathIdleColors[8] = {
    LEDColor(0, 255, 128),   LEDColor(0, 200, 100),
    LEDColor(50, 255, 150),  LEDColor(0, 180, 80),
    LEDColor(0, 255, 128),   LEDColor(0, 200, 100),
    LEDColor(50, 255, 150),  LEDColor(0, 180, 80),
};

// Idle state — emerald pulse
const LEDState CIPHER_PATH_IDLE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(
            cipherPathIdleColors[i % 8], 65);
        state.rightLights[i] = LEDState::SingleLEDState(
            cipherPathIdleColors[i % 8], 65);
    }
    return state;
}();

// Gameplay state — emerald chase
const LEDState CIPHER_PATH_GAMEPLAY_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(100 + (i * 15));
        state.leftLights[i] = LEDState::SingleLEDState(
            cipherPathColors[i % 4], brightness);
        state.rightLights[i] = LEDState::SingleLEDState(
            cipherPathColors[i % 4], brightness);
    }
    return state;
}();

// Win state — bright emerald sweep
const LEDState CIPHER_PATH_WIN_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t g = static_cast<uint8_t>(255 - (i * 10));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 128), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 128), 255);
    }
    return state;
}();

// Lose state — red fade
const LEDState CIPHER_PATH_LOSE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(255 - (i * 25));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
    }
    return state;
}();

// Correct move flash — green
const LEDState CIPHER_PATH_CORRECT_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
    }
    return state;
}();

// Wrong move flash — red
const LEDState CIPHER_PATH_WRONG_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
    }
    return state;
}();
