#pragma once

#include "device/drivers/light-interface.hpp"
#include <cstdint>

/*
 * Cipher Path rendering constants and LED palettes.
 * Wire routing puzzle with real-time electricity flow visualization.
 */

// Grid layout constants
constexpr int CELL_PITCH = 9;           // pixels between cell origins (8x8 inner + 1px border)
constexpr int CELL_INNER = 8;           // inner cell rendering area
constexpr int WIRE_WIDTH = 2;           // path wire thickness (2px bold)
constexpr int NOISE_WIDTH = 1;          // noise wire thickness (1px thin)

// Tile type enumeration
enum TileType {
    TILE_EMPTY = 0,      // No wire segment
    TILE_STRAIGHT = 1,   // Line (2 opposite edges)
    TILE_ELBOW = 2,      // 90° bend (2 adjacent edges)
    TILE_T_JUNCTION = 3, // T-shape (3 edges)
    TILE_CROSS = 4,      // + shape (all 4 edges)
    TILE_ENDPOINT = 5    // Terminal (1 edge)
};

// Direction bits for wire connections (can be OR'd together)
constexpr int DIR_UP    = 1 << 0;  // 0x01
constexpr int DIR_RIGHT = 1 << 1;  // 0x02
constexpr int DIR_DOWN  = 1 << 2;  // 0x04
constexpr int DIR_LEFT  = 1 << 3;  // 0x08

// Tile rotation to connection mapping (rotation × 90°)
// Returns DIR_* bitmask for each tile type at each rotation
inline int getTileConnections(int tileType, int rotation) {
    // Base connections for rotation 0
    int base = 0;
    switch (tileType) {
        case TILE_STRAIGHT:   base = DIR_LEFT | DIR_RIGHT; break;   // Horizontal line
        case TILE_ELBOW:      base = DIR_UP | DIR_RIGHT; break;     // Up-right bend
        case TILE_T_JUNCTION: base = DIR_UP | DIR_RIGHT | DIR_DOWN; break;  // Missing left
        case TILE_CROSS:      base = DIR_UP | DIR_RIGHT | DIR_DOWN | DIR_LEFT; break;
        case TILE_ENDPOINT:   base = DIR_RIGHT; break;              // Pointing right
        default: return 0;
    }

    // Rotate connections clockwise by rotation steps
    int result = 0;
    for (int i = 0; i < 4; i++) {
        if (base & (1 << i)) {
            int newDir = (i + rotation) % 4;
            result |= (1 << newDir);
        }
    }
    return result;
}

// LED palettes — emerald "circuit" theme

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

// Gameplay state — emerald electrical chase
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

// Win state — bright emerald power wave
const LEDState CIPHER_PATH_WIN_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t g = static_cast<uint8_t>(255 - (i * 10));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 128), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, g, 128), 255);
    }
    return state;
}();

// Lose state — red circuit break surge
const LEDState CIPHER_PATH_LOSE_STATE = [](){
    LEDState state;
    for (int i = 0; i < 9; i++) {
        uint8_t brightness = static_cast<uint8_t>(255 - (i * 25));
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 0, 0), brightness);
    }
    return state;
}();
