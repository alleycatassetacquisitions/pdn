#pragma once

#include <cstdlib>
#include <cstring>
#include "device/device.hpp"
#include "device/drivers/display.hpp"
#include "game/image-resources.hpp"
#include "utils/simple-timer.hpp"

constexpr int GLYPH_LOADING_DURATION_MS = 500;
constexpr int SCREEN_DISPLAY_WIDTH = 128;
constexpr int CHAR_WIDTH = 10;

inline int centeredTextX(const char* text) {
    int startPoint = (SCREEN_DISPLAY_WIDTH - static_cast<int>(strlen(text)) * CHAR_WIDTH) / 2;
    if(startPoint < 0) {
        startPoint = 0;
    }
    return startPoint;
}

inline void showLoadingGlyphs(Device* PDN) {
    constexpr int GLYPH_SIZE    = 14;
    constexpr int SCREEN_WIDTH  = 128;
    constexpr int SCREEN_HEIGHT = 64;
    constexpr int GLYPHS_PER_ROW = SCREEN_WIDTH / GLYPH_SIZE;
    constexpr int GLYPHS_PER_COL = 11;

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::LOADING_GLYPH);

    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if (rand() % 100 < 50) {
                int x = col * GLYPH_SIZE;
                int y = row * GLYPH_SIZE;
                PDN->getDisplay()->renderGlyph(loadingGlyphs[rand() % 8], x, y);
            }
        }
    }

    PDN->getDisplay()->render();
}

inline bool isInGlyphLoadingPhase(Device* PDN, SimpleTimer& timer) {
    if (!timer.expired()) {
        showLoadingGlyphs(PDN);
        return true;
    }
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    return false;
}
