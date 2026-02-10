#pragma once

#include "device/drivers/light-interface.hpp"
#include "game/signal-echo/signal-echo.hpp"

/*
 * Signal Echo LED palettes.
 *
 * signalEchoColors/signalEchoIdleColors: Used by BOTH the NPC device
 * (walking advertisement) and by players who beat hard mode and equip
 * the profile. What you see on the NPC is what you get when you win.
 *
 * These follow the same LEDColor array format as quickdraw-resources.hpp.
 */

// Primary palette (4 colors) — used for non-idle animations
const LEDColor signalEchoColors[4] = {
    LEDColor(255, 0, 255),   // Magenta
    LEDColor(200, 0, 200),   // Dark magenta
    LEDColor(255, 50, 200),  // Pink
    LEDColor(180, 0, 255),   // Purple
};

// Idle palette (8 colors) — used for idle breathing animation
const LEDColor signalEchoIdleColors[8] = {
    LEDColor(255, 0, 255),   LEDColor(200, 0, 200),
    LEDColor(255, 50, 200),  LEDColor(180, 0, 255),
    LEDColor(255, 0, 255),   LEDColor(200, 0, 200),
    LEDColor(255, 50, 200),  LEDColor(180, 0, 255),
};

// Default/neutral palette — used when no color profile equipped
// Used by Features C/D (color profile application to device LEDs)
const LEDColor defaultProfileColors[4] = {
    LEDColor(255, 255, 100),  // Warm yellow
    LEDColor(255, 255, 200),  // Pale yellow
    LEDColor(255, 255, 255),  // White
    LEDColor(200, 200, 150),  // Dim warm
};

// Used by Features C/D (color profile application to device LEDs)
const LEDColor defaultProfileIdleColors[8] = {
    LEDColor(255, 255, 100),  LEDColor(255, 255, 255),
    LEDColor(255, 255, 200),  LEDColor(200, 200, 150),
    LEDColor(255, 255, 100),  LEDColor(255, 255, 255),
    LEDColor(255, 255, 200),  LEDColor(200, 200, 150),
};

// Signal Echo intro idle LED state — magenta/pink pulse
const LEDState SIGNAL_ECHO_IDLE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(
            signalEchoIdleColors[i % 8], 65);
        state.rightLights[i] = LEDState::SingleLEDState(
            signalEchoIdleColors[i % 8], 65);
    }
    return state;
}();

// Win state — rainbow sweep using bright colors
const LEDColor winRainbowColors[9] = {
    LEDColor(255, 0, 0),     // Red
    LEDColor(255, 127, 0),   // Orange
    LEDColor(255, 255, 0),   // Yellow
    LEDColor(0, 255, 0),     // Green
    LEDColor(0, 255, 255),   // Cyan
    LEDColor(0, 127, 255),   // Sky blue
    LEDColor(0, 0, 255),     // Blue
    LEDColor(127, 0, 255),   // Violet
    LEDColor(255, 0, 255),   // Magenta
};

const LEDState SIGNAL_ECHO_WIN_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(winRainbowColors[i], 255);
        state.rightLights[i] = LEDState::SingleLEDState(winRainbowColors[8 - i], 255);
    }
    return state;
}();

// Lose state — red fade
const LEDState SIGNAL_ECHO_LOSE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(255 - (i * 25));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
    }
    return state;
}();

// Error flash — brief red flash on wrong input
const LEDState SIGNAL_ECHO_ERROR_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
    }
    return state;
}();

// Correct input flash — brief green flash
const LEDState SIGNAL_ECHO_CORRECT_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, 255, 0), 255);
    }
    return state;
}();

// Difficulty presets
const SignalEchoConfig SIGNAL_ECHO_EASY = {
    .sequenceLength = 4,
    .numSequences = 4,
    .displaySpeedMs = 600,
    .timeLimitMs = 0,
    .cumulative = false,
    .allowedMistakes = 3,
    .rngSeed = 0,
    .managedMode = false,
};

const SignalEchoConfig SIGNAL_ECHO_HARD = {
    .sequenceLength = 8,
    .numSequences = 4,
    .displaySpeedMs = 400,
    .timeLimitMs = 0,
    .cumulative = false,
    .allowedMistakes = 1,
    .rngSeed = 0,
    .managedMode = false,
};
