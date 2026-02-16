#pragma once

#include "device/drivers/light-interface.hpp"
#include "device/drivers/display.hpp"
#include <cstdint>

/*
 * Spike Vector visual resources — XBM sprites and LED palettes
 */

// Right-pointing triangle cursor (5x7 XBM)
static const uint8_t CURSOR_SPRITE_DATA[] = {
    0x01, 0x03, 0x07, 0x0F, 0x07, 0x03, 0x01
};

static const Image CURSOR_SPRITE = Image(
    CURSOR_SPRITE_DATA,
    5,   // width
    7,   // height
    0,   // defaultStartX
    0    // defaultStartY
);

/*
 * Spike Vector LED palettes — magenta/purple theme.
 * Represents the approaching spike walls.
 */

// Primary palette (4 colors) — used for gameplay animations
const LEDColor spikeVectorColors[4] = {
    LEDColor(200, 0, 255),   // Purple
    LEDColor(255, 0, 200),   // Magenta-pink
    LEDColor(150, 0, 200),   // Dark purple
    LEDColor(255, 50, 255),  // Bright magenta
};

// Idle palette (8 colors) — used for idle breathing animation
const LEDColor spikeVectorIdleColors[8] = {
    LEDColor(200, 0, 255),   LEDColor(150, 0, 200),
    LEDColor(255, 0, 200),   LEDColor(180, 0, 255),
    LEDColor(200, 0, 255),   LEDColor(150, 0, 200),
    LEDColor(255, 0, 200),   LEDColor(180, 0, 255),
};

// Intro/idle LED state — magenta/purple pulse
const LEDState SPIKE_VECTOR_IDLE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(
            spikeVectorIdleColors[i % 8], 65);
        state.rightLights[i] = LEDState::SingleLEDState(
            spikeVectorIdleColors[i % 8], 65);
    }
    return state;
}();

// Gameplay LED state — chase animation during wall approach
const LEDState SPIKE_VECTOR_GAMEPLAY_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(100 + (i * 17));
        state.leftLights[i] = LEDState::SingleLEDState(
            spikeVectorColors[i % 4], brightness);
        state.rightLights[i] = LEDState::SingleLEDState(
            spikeVectorColors[i % 4], brightness);
    }
    return state;
}();

// Win state — bright purple/magenta sweep
const LEDState SPIKE_VECTOR_WIN_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t g = static_cast<uint8_t>(i * 28);
        state.leftLights[i] = LEDState::SingleLEDState(
            LEDColor(200, g, 255), 255);
        state.rightLights[i] = LEDState::SingleLEDState(
            LEDColor(200, g, 255), 255);
    }
    return state;
}();

// Lose state — red fade
const LEDState SPIKE_VECTOR_LOSE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(255 - (i * 25));
        state.leftLights[i] = LEDState::SingleLEDState(
            LEDColor(255, 0, 0), brightness);
        state.rightLights[i] = LEDState::SingleLEDState(
            LEDColor(255, 0, 0), brightness);
    }
    return state;
}();
