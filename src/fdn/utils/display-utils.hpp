#pragma once

#include <cstdlib>
#include <cstring>
#include "device/drivers/display.hpp"
#include "utils/simple-timer.hpp"

constexpr int GLYPH_LOADING_DURATION_MS = 500;
constexpr int FDN_SCREEN_WIDTH = 128;
constexpr int FDN_CHAR_WIDTH   = 10;

static const char* fdnLoadingGlyphs[] = {
    "\u2630", "\u2631", "\u2632", "\u2633",
    "\u2634", "\u2635", "\u2636", "\u2637"
};

inline int centeredTextX(const char* text) {
    if (text == nullptr) return 0;
    int start = (FDN_SCREEN_WIDTH - static_cast<int>(strlen(text)) * FDN_CHAR_WIDTH) / 2;
    return (start < 0) ? 0 : start;
}

inline void showLoadingGlyphs(Display* display) {
    constexpr int GLYPH_SIZE    = 14;
    constexpr int SCREEN_WIDTH  = 128;
    constexpr int GLYPHS_PER_ROW = SCREEN_WIDTH / GLYPH_SIZE;
    constexpr int GLYPHS_PER_COL = 5;

    display->invalidateScreen();
    display->setGlyphMode(FontMode::LOADING_GLYPH);

    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if (rand() % 100 < 50) {
                display->renderGlyph(
                    fdnLoadingGlyphs[rand() % 8],
                    col * GLYPH_SIZE,
                    14 + row * GLYPH_SIZE);
            }
        }
    }

    display->render();
}

inline bool isInGlyphLoadingPhase(Display* display, SimpleTimer& timer) {
    if (!timer.expired()) {
        showLoadingGlyphs(display);
        return true;
    }
    display->setGlyphMode(FontMode::TEXT);
    return false;
}
