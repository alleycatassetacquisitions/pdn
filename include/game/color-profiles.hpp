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
