#pragma once

#include "game/quickdraw-resources.hpp"

/** Event NodeNetwork artwork (Alleycat image set). */
inline Image nodeNetworkImage(ImageType whichImage) {
    return getImageForAllegiance(Allegiance::ALLEYCAT, whichImage);
}

/** Strong greens for the bounty-style vertical chase on Node idle. */
inline const LEDColor nodeNetworkIdleChaseColors[4] = {
    LEDColor(0, 255, 48),
    LEDColor(0, 210, 90),
    LEDColor(32, 255, 0),
    LEDColor(0, 180, 70),
};

/** Win-screen grip colors (copy of the hunter palette; first 6 are used, 3 per side). */
inline const LEDColor nodeNetworkWinLEDColors[8] = {
    LEDColor(0, 255, 0),     // Green
    LEDColor(0, 237, 75),    // Blue-green
    LEDColor(0, 200, 100),   // Teal
    LEDColor(0, 180, 180),   // Cyan
    LEDColor(0, 255, 0),     // Green
    LEDColor(0, 237, 75),    // Blue-green
    LEDColor(0, 200, 100),   // Teal
    LEDColor(0, 180, 180)    // Cyan
};

/** Initial LED state for `VerticalChaseAnimation` on Node idle (mirrors bounty idle layout). */
inline const LEDState NODE_NETWORK_IDLE_LED_STATE = []() {
    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
    }
    state.transmitLight = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
    for (int i = 0; i < 4; i++) {
        state.leftLights[i] =
            LEDState::SingleLEDState(nodeNetworkIdleChaseColors[i], 255);
        state.rightLights[i] =
            LEDState::SingleLEDState(nodeNetworkIdleChaseColors[i], 255);
    }
    return state;
}();
