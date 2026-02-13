#pragma once

#include "device/drivers/light-interface.hpp"
#include "device/device-types.hpp"

/*
 * Color profile lookup.
 *
 * Each minigame has a unique 9-LED palette that serves as a walking
 * advertisement on the FDN NPC and as an equippable idle animation
 * for players who beat hard mode.
 *
 * getColorProfileState(gameType) returns a const ref to the LEDState.
 * Returns a default neutral palette if the game type has no profile.
 */

// Signal Echo palette — magenta/pink/purple
const LEDState SIGNAL_ECHO_PROFILE_STATE = [](){
    LEDState state;
    LEDColor colors[9] = {
        {255,0,255}, {200,0,200}, {255,50,200}, {180,0,255},
        {255,0,255}, {200,0,200}, {255,50,200}, {180,0,255}, {180,0,255}
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(colors[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(colors[i], 65);
    }
    return state;
}();

// Firewall Decrypt palette — green/cyan hacker theme
const LEDState FIREWALL_DECRYPT_PROFILE_STATE = [](){
    LEDState state;
    LEDColor colors[9] = {
        {0,255,100}, {0,200,150}, {0,255,200}, {0,150,255},
        {0,255,100}, {0,200,150}, {0,255,200}, {0,150,255}, {0,150,255}
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(colors[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(colors[i], 65);
    }
    return state;
}();

// Ghost Runner palette — cyan/white/pale blue
const LEDState GHOST_RUNNER_PROFILE_STATE = [](){
    LEDState state;
    LEDColor colors[9] = {
        {0,255,255}, {100,255,255}, {200,255,255}, {150,200,255},
        {0,255,255}, {100,255,255}, {200,255,255}, {150,200,255}, {150,200,255}
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(colors[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(colors[i], 65);
    }
    return state;
}();

// Spike Vector palette — yellow/orange/red
const LEDState SPIKE_VECTOR_PROFILE_STATE = [](){
    LEDState state;
    LEDColor colors[9] = {
        {255,200,0}, {255,150,0}, {255,100,0}, {255,50,0},
        {255,200,0}, {255,150,0}, {255,100,0}, {255,50,0}, {255,50,0}
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(colors[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(colors[i], 65);
    }
    return state;
}();

// Cipher Path palette — green/lime/teal
const LEDState CIPHER_PATH_PROFILE_STATE = [](){
    LEDState state;
    LEDColor colors[9] = {
        {0,255,50}, {50,255,100}, {0,200,80}, {0,255,150},
        {0,255,50}, {50,255,100}, {0,200,80}, {0,255,150}, {0,255,150}
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(colors[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(colors[i], 65);
    }
    return state;
}();

// Exploit Sequencer palette — red/dark purple/crimson
const LEDState EXPLOIT_SEQUENCER_PROFILE_STATE = [](){
    LEDState state;
    LEDColor colors[9] = {
        {255,0,50}, {200,0,100}, {180,0,80}, {150,0,60},
        {255,0,50}, {200,0,100}, {180,0,80}, {150,0,60}, {150,0,60}
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(colors[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(colors[i], 65);
    }
    return state;
}();

// Breach Defense palette — blue/silver/white
const LEDState BREACH_DEFENSE_PROFILE_STATE = [](){
    LEDState state;
    LEDColor colors[9] = {
        {0,100,255}, {50,150,255}, {100,200,255}, {200,200,255},
        {0,100,255}, {50,150,255}, {100,200,255}, {200,200,255}, {200,200,255}
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(colors[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(colors[i], 65);
    }
    return state;
}();

// Default/neutral palette — warm yellow/white
const LEDState DEFAULT_PROFILE_STATE = [](){
    LEDState state;
    LEDColor colors[9] = {
        {255,255,100}, {255,255,200}, {255,255,255}, {200,200,150},
        {255,255,100}, {255,255,200}, {255,255,255}, {200,200,150}, {200,200,150}
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(colors[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(colors[i], 65);
    }
    return state;
}();

/*
 * Look up the LED profile state for a given game type.
 * Returns DEFAULT_PROFILE_STATE if no specific profile exists.
 */
inline const LEDState& getColorProfileState(int gameType) {
    switch (static_cast<GameType>(gameType)) {
        case GameType::SIGNAL_ECHO:       return SIGNAL_ECHO_PROFILE_STATE;
        case GameType::FIREWALL_DECRYPT:  return FIREWALL_DECRYPT_PROFILE_STATE;
        case GameType::GHOST_RUNNER:      return GHOST_RUNNER_PROFILE_STATE;
        case GameType::SPIKE_VECTOR:      return SPIKE_VECTOR_PROFILE_STATE;
        case GameType::CIPHER_PATH:       return CIPHER_PATH_PROFILE_STATE;
        case GameType::EXPLOIT_SEQUENCER: return EXPLOIT_SEQUENCER_PROFILE_STATE;
        case GameType::BREACH_DEFENSE:    return BREACH_DEFENSE_PROFILE_STATE;
        default:                          return DEFAULT_PROFILE_STATE;
    }
}

/*
 * Get display name for a color profile.
 */
inline const char* getColorProfileName(int gameType, bool isHunter) {
    if (gameType < 0) return isHunter ? "HUNTER DEFAULT" : "BOUNTY DEFAULT";
    return getGameDisplayName(static_cast<GameType>(gameType));
}
