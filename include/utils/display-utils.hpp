#pragma once

#include <cstdlib>
#include "device/device.hpp"
#include "device/drivers/display.hpp"
#include "game/image-resources.hpp"
#include "utils/simple-timer.hpp"

constexpr int GLYPH_LOADING_DURATION_MS = 500;

inline void showLoadingGlyphs(Device* PDN) {
    constexpr int GLYPH_SIZE    = 14;
    constexpr int SCREEN_WIDTH  = 128;
    constexpr int SCREEN_HEIGHT = 64;
    constexpr int GLYPHS_PER_ROW = SCREEN_WIDTH / GLYPH_SIZE;
    constexpr int GLYPHS_PER_COL = (SCREEN_HEIGHT - GLYPH_SIZE) / GLYPH_SIZE;

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::LOADING_GLYPH);

    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if (rand() % 100 < 50) {
                int x = col * GLYPH_SIZE;
                int y = GLYPH_SIZE + (row * GLYPH_SIZE);
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
    return false;
}
