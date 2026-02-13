#pragma once

#include "device/drivers/light-interface.hpp"
#include <cstdint>

/*
 * Ghost Runner LED palettes.
 *
 * ghostRunnerColors: cyan/teal theme — represents the digital ghost
 * moving through the system. What you see on the NPC is what you
 * get when you win hard mode.
 */

// Primary palette (4 colors) — used for non-idle animations
const LEDColor ghostRunnerColors[4] = {
    LEDColor(0, 255, 200),    // Cyan-green
    LEDColor(0, 200, 180),    // Teal
    LEDColor(0, 255, 255),    // Cyan
    LEDColor(0, 180, 200),    // Dark cyan
};

// Idle palette (8 colors) — used for idle breathing animation
const LEDColor ghostRunnerIdleColors[8] = {
    LEDColor(0, 255, 200),   LEDColor(0, 200, 180),
    LEDColor(0, 255, 255),   LEDColor(0, 180, 200),
    LEDColor(0, 255, 200),   LEDColor(0, 200, 180),
    LEDColor(0, 255, 255),   LEDColor(0, 180, 200),
};

// Ghost Runner intro idle LED state — cyan/teal pulse
const LEDState GHOST_RUNNER_IDLE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(
            ghostRunnerIdleColors[i % 8], 65);
        state.rightLights[i] = LEDState::SingleLEDState(
            ghostRunnerIdleColors[i % 8], 65);
    }
    return state;
}();

// Gameplay state — ghost position indicated by chase animation
const LEDState GHOST_RUNNER_GAMEPLAY_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        LEDColor color = ghostRunnerColors[i % 4];
        state.leftLights[i] = LEDState::SingleLEDState(color, 200);
        state.rightLights[i] = LEDState::SingleLEDState(color, 200);
    }
    return state;
}();

// Win state — bright green sweep
const LEDState GHOST_RUNNER_WIN_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t g = static_cast<uint8_t>(200 + (i * 6));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 100), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 100), 255);
    }
    return state;
}();

// Lose state — red fade
const LEDState GHOST_RUNNER_LOSE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(255 - (i * 25));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
    }
    return state;
}();

// Hit flash — brief green flash on correct timing
const LEDState GHOST_RUNNER_HIT_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
    }
    return state;
}();

// Miss flash — brief red flash on wrong timing
const LEDState GHOST_RUNNER_MISS_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
    }
    return state;
}();
