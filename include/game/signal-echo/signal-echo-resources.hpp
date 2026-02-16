#pragma once

#include "device/drivers/light-interface.hpp"
#include "game/signal-echo/signal-echo.hpp"

/*
 * Signal Echo LED palettes.
 *
 * signalEchoColors/signalEchoIdleColors: Used by BOTH the NPC device
 * (walking advertisement) and by players who beat hard mode and equip
 * the profile. What you see on the NPC is what you get when you win.
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
const LEDColor defaultProfileColors[4] = {
    LEDColor(255, 255, 100),  // Warm yellow
    LEDColor(255, 255, 200),  // Pale yellow
    LEDColor(255, 255, 255),  // White
    LEDColor(200, 200, 150),  // Dim warm
};

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
inline SignalEchoConfig makeEasyConfig() {
    SignalEchoConfig c;
    c.sequenceLength = 7;
    c.numSequences = 3;
    c.displaySpeedMs = 550;
    c.timeLimitMs = 0;
    c.cumulative = false;
    c.allowedMistakes = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

inline SignalEchoConfig makeHardConfig() {
    SignalEchoConfig c;
    c.sequenceLength = 11;
    c.numSequences = 5;
    c.displaySpeedMs = 400;
    c.timeLimitMs = 0;
    c.cumulative = false;
    c.allowedMistakes = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

const SignalEchoConfig SIGNAL_ECHO_EASY = makeEasyConfig();
const SignalEchoConfig SIGNAL_ECHO_HARD = makeHardConfig();

/*
 * Signal Echo renderer — reusable drawing helpers for pixel arrow glyphs
 * and framed slot layout.
 */
namespace SignalEchoRenderer {

struct SlotLayout {
    int startX;
    int slotWidth;
    int slotHeight;
    int gap;
    int startY;
    int count;
};

inline SlotLayout getLayout(int sequenceLength) {
    SlotLayout layout;
    layout.count = sequenceLength;
    layout.slotHeight = 26;
    layout.startY = 18;

    if (sequenceLength <= 7) {
        // Easy mode (7 slots)
        layout.slotWidth = 14;
        layout.gap = 3;
        layout.startX = 6;
    } else {
        // Hard mode (11 slots)
        layout.slotWidth = 10;
        layout.gap = 1;
        layout.startX = 4;
    }

    return layout;
}

inline void drawUpArrow(Display* d, int x, int y, int cellW, int cellH) {
    if (cellW >= 14) {
        // Easy mode (14px wide)
        d->drawBox(x + 5, y + 2, 4, 2);    // tip
        d->drawBox(x + 3, y + 4, 8, 2);    // mid
        d->drawBox(x + 1, y + 6, 12, 2);   // base
        d->drawBox(x + 5, y + 8, 4, 14);   // shaft
    } else {
        // Hard mode (10px wide)
        d->drawBox(x + 3, y + 2, 4, 2);    // tip
        d->drawBox(x + 1, y + 4, 8, 2);    // base-head
        d->drawBox(x + 3, y + 6, 4, 16);   // shaft
    }
}

inline void drawDownArrow(Display* d, int x, int y, int cellW, int cellH) {
    if (cellW >= 14) {
        // Easy mode (14px wide)
        d->drawBox(x + 5, y + 2, 4, 14);   // shaft
        d->drawBox(x + 1, y + 16, 12, 2);  // base
        d->drawBox(x + 3, y + 18, 8, 2);   // mid
        d->drawBox(x + 5, y + 20, 4, 2);   // tip
    } else {
        // Hard mode (10px wide)
        d->drawBox(x + 3, y + 2, 4, 16);   // shaft
        d->drawBox(x + 1, y + 18, 8, 2);   // base-head
        d->drawBox(x + 3, y + 20, 4, 2);   // tip
    }
}

inline void drawSlotEmpty(Display* d, int x, int y, int w, int h) {
    d->drawFrame(x, y, w, h);
}

inline void drawSlotFilled(Display* d, int x, int y, int w, int h, bool isUp) {
    d->drawFrame(x, y, w, h);
    if (isUp) {
        drawUpArrow(d, x, y, w, h);
    } else {
        drawDownArrow(d, x, y, w, h);
    }
}

inline void drawSlotActive(Display* d, int x, int y, int w, int h) {
    d->drawBox(x, y, w, h);  // filled white
}

inline void drawAllSlots(Display* d, const SlotLayout& layout,
                         const std::vector<bool>& sequence,
                         int revealCount,
                         int activeSlot = -1) {
    for (int i = 0; i < layout.count; i++) {
        int x = layout.startX + i * (layout.slotWidth + layout.gap);
        int y = layout.startY;

        if (i == activeSlot) {
            // Active cursor — inverted
            drawSlotActive(d, x, y, layout.slotWidth, layout.slotHeight);
        } else if (i < revealCount) {
            // Revealed/input signal
            drawSlotFilled(d, x, y, layout.slotWidth, layout.slotHeight, sequence[i]);
        } else {
            // Empty future slot
            drawSlotEmpty(d, x, y, layout.slotWidth, layout.slotHeight);
        }
    }
}

inline void drawHUD(Display* d, int currentRound, int totalRounds) {
    // Left: round label
    std::string roundLabel = "SEQ " + std::to_string(currentRound + 1) +
                             "/" + std::to_string(totalRounds);
    d->setGlyphMode(FontMode::TEXT)->drawText(roundLabel.c_str(), 2, 6);

    // Right: progress pips
    int pipSize = 4;
    int pipGap = 2;
    int pipTotal = totalRounds * (pipSize + pipGap) - pipGap;
    int pipStartX = 128 - pipTotal - 2;

    for (int i = 0; i < totalRounds; i++) {
        int pipX = pipStartX + i * (pipSize + pipGap);
        if (i < currentRound) {
            d->drawBox(pipX, 2, pipSize, pipSize);  // filled
        } else {
            d->drawFrame(pipX, 2, pipSize, pipSize);  // outline
        }
    }
}

inline void drawSeparators(Display* d) {
    d->drawBox(0, 8, 128, 1);   // top separator
    d->drawBox(0, 55, 128, 1);  // bottom separator
}

inline void drawControls(Display* d, bool showButtons) {
    if (showButtons) {
        d->setGlyphMode(FontMode::TEXT)
            ->drawText("[^]", 2, 62)
            ->drawText("[v]", 108, 62);
    }
}

inline void drawProgressBar(Display* d, int current, int total) {
    int barWidth = 80;
    int barHeight = 4;
    int barX = (128 - barWidth) / 2;
    int barY = 47;

    d->drawFrame(barX, barY, barWidth, barHeight);

    if (total > 0) {
        int fillWidth = (current * barWidth) / total;
        if (fillWidth > 0) {
            d->drawBox(barX, barY, fillWidth, barHeight);
        }
    }
}

}  // namespace SignalEchoRenderer
