#pragma once

#include "device/drivers/display.hpp"
#include <cstdio>

/*
 * UI Clean Minimal Design System
 *
 * Provides helper functions for rendering clean, minimal UI screens
 * using u8g2 icon fonts and geometric layouts.
 *
 * Design principles:
 * - Maximum white space
 * - Simple geometric icons (Open Iconic)
 * - Thin lines, subtle borders
 * - Ultra-compact status elements
 */

namespace UICleanMinimal {

// Icon font glyphs (Open Iconic)
namespace Icons {
    // Check 2x (16x16) - u8g2_font_open_iconic_check_2x_t
    const uint16_t CHECK_MARK = 64;      // ✓
    const uint16_t X_MARK = 66;          // ✗
    const uint16_t CHECK_CIRCLE = 67;    // ✓ in circle
    const uint16_t X_CIRCLE = 71;        // ✗ in circle

    // Play 2x (16x16) - u8g2_font_open_iconic_play_2x_t
    const uint16_t PLAY = 80;            // ▶
    const uint16_t PAUSE = 81;           // ⏸
    const uint16_t STOP = 82;            // ⏹

    // GUI 1x (8x8) - u8g2_font_open_iconic_gui_1x_t
    const uint16_t FILLED_CIRCLE = 69;   // ●
    const uint16_t EMPTY_CIRCLE = 70;    // ○
    const uint16_t FILLED_BOX = 73;      // ▣
    const uint16_t EMPTY_BOX = 74;       // ▢

    // Arrow 1x (8x8) - u8g2_font_open_iconic_arrow_1x_t
    const uint16_t ARROW_LEFT = 64;      // ←
    const uint16_t ARROW_RIGHT = 65;     // →
    const uint16_t ARROW_UP = 66;        // ↑
    const uint16_t ARROW_DOWN = 67;      // ↓
    const uint16_t REFRESH = 73;         // ↻
}

// Layout constants
namespace Layout {
    const int SCREEN_WIDTH = 128;
    const int SCREEN_HEIGHT = 64;
    const int MARGIN = 4;
    const int STATUS_BAR_HEIGHT = 8;
    const int CONTENT_Y_START = 10;
}

/*
 * Draw a large centered icon with text below
 * Used for win/lose screens
 */
inline void drawCenteredIconWithText(Display* display,
                                     uint16_t iconGlyph,
                                     const char* iconFont,
                                     const char* primaryText,
                                     const char* primaryFont,
                                     const char* secondaryText = nullptr,
                                     const char* tertiaryText = nullptr) {
    display->invalidateScreen();

    // Note: u8g2 doesn't expose setFont directly through Display interface
    // We need to use setGlyphMode and rely on the fonts set there
    // For now, we'll use drawText which uses the current font

    // This is a simplified version - full implementation needs direct u8g2 access
    // or extended Display interface

    display->drawText(primaryText, 38, 38);

    if (secondaryText) {
        display->drawText(secondaryText, 30, 50);
    }

    if (tertiaryText) {
        display->drawText(tertiaryText, 24, 60);
    }

    display->render();
}

/*
 * Draw win screen with victory icon
 */
inline void drawWinScreen(Display* display, int score, int attempts = 0) {
    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // Victory text - centered
    display->drawText("VICTORY!", 38, 38);

    // Score
    char scoreText[32];
    snprintf(scoreText, sizeof(scoreText), "Score: %d", score);
    display->drawText(scoreText, 35, 50);

    // Attempts (if provided)
    if (attempts > 0) {
        char attemptText[32];
        snprintf(attemptText, sizeof(attemptText), "Attempt #%d", attempts);
        display->drawText(attemptText, 30, 26);
    }

    // Continue prompt
    display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    display->drawText("PRESS TO CONTINUE", 18, 60);

    display->render();
}

/*
 * Draw lose screen with failure icon
 */
inline void drawLoseScreen(Display* display, int score, int attemptsRemaining = 0) {
    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // Failed text - centered
    display->drawText("FAILED", 42, 38);

    // Score
    char scoreText[32];
    snprintf(scoreText, sizeof(scoreText), "Score: %d", score);
    display->drawText(scoreText, 35, 50);

    // Attempts or game over
    if (attemptsRemaining > 0) {
        char attemptsText[32];
        snprintf(attemptsText, sizeof(attemptsText), "Attempts: %d/3", 3 - attemptsRemaining);
        display->drawText(attemptsText, 28, 26);

        // Retry prompt
        display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
        display->drawText("Press to retry", 32, 60);
    } else {
        display->drawText("GAME OVER", 32, 26);
    }

    display->render();
}

/*
 * Draw game intro screen
 */
inline void drawGameIntro(Display* display,
                         const char* gameName,
                         bool hardMode,
                         const char* instruction = "Press to start") {
    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // Game name - centered at top
    int gameNameLen = 0;
    while (gameName[gameNameLen]) gameNameLen++;
    int gameNameX = (Layout::SCREEN_WIDTH - gameNameLen * 6) / 2; // Approx 6px per char
    display->drawText(gameName, gameNameX, 28);

    // Difficulty label
    display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    display->drawText("DIFFICULTY", 48, 38);

    // Difficulty value
    display->setGlyphMode(FontMode::TEXT);
    display->drawText(hardMode ? "HARD" : "EASY", 52, 48);

    // Instruction
    display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    int instrLen = 0;
    while (instruction[instrLen]) instrLen++;
    int instrX = (Layout::SCREEN_WIDTH - instrLen * 5) / 2; // Approx 5px per char for small
    display->drawText(instruction, instrX, 60);

    display->render();
}

/*
 * Draw Konami progress screen
 */
inline void drawKonamiProgress(Display* display,
                               bool collected[7],
                               int totalCollected) {
    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // Title
    display->drawText("KONAMI PROGRESS", 22, 18);

    // Button slots - simple text representation
    // In full implementation with u8g2 access, we'd use icon glyphs
    for (int i = 0; i < 7; i++) {
        int x = 16 + i * 16;
        display->drawText(collected[i] ? "X" : "O", x, 36);

        // Number below
        char num[2];
        num[0] = '1' + static_cast<char>(i);
        num[1] = '\0';
        display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
        display->drawText(num, x + 2, 46);
        display->setGlyphMode(FontMode::TEXT);
    }

    // Progress count
    char progressText[16];
    snprintf(progressText, sizeof(progressText), "%d / 7", totalCollected);
    display->drawText(progressText, 56, 56);

    display->render();
}

/*
 * Draw minimal HUD bar (8px height at top)
 */
inline void drawGameHUD(Display* display,
                       int lives,
                       int score,
                       int timePercent,
                       bool hardMode) {
    // HUD rendering at top 8px
    // Note: This requires direct u8g2 access for drawBox/drawFrame
    // Simplified version uses text representation

    display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);

    // Lives (hearts representation)
    char livesText[8];
    for (int i = 0; i < lives && i < 3; i++) {
        livesText[i] = 'H';
    }
    livesText[lives] = '\0';
    display->drawText(livesText, 2, 7);

    // Score
    char scoreText[16];
    snprintf(scoreText, sizeof(scoreText), "%d", score);
    display->drawText(scoreText, 24, 7);

    // Timer bar (text representation)
    // Full implementation would use drawBox/drawFrame

    // Difficulty
    display->drawText(hardMode ? "H" : "E", 118, 7);
}

} // namespace UICleanMinimal

/*
 * Bold Retro UI Helper Functions
 *
 * Cherry-picked from Design B (PR #78) for high-contrast, geometric rendering.
 * These leverage the extended Display interface (drawBox, drawFrame, setDrawColor)
 * for better readability on 128x64 OLED displays.
 */
namespace BoldRetroUI {

/*
 * Draw a full-width inverted header bar
 * Creates high-contrast section headers
 */
inline void drawHeaderBar(Display* display, const char* text) {
    display->drawBox(0, 0, 128, 10)
           ->setDrawColor(0)
           ->drawText(text, 2, 9)
           ->setDrawColor(1);
}

/*
 * Draw a double-bordered frame around content
 * Provides strong visual emphasis for scores and status
 */
inline void drawBorderedFrame(Display* display, int x, int y, int w, int h) {
    display->drawFrame(x, y, w, h)
           ->drawFrame(x+1, y+1, w-2, h-2);
}

/*
 * Draw horizontally centered text
 * Assumes 6px average character width
 */
inline void drawCenteredText(Display* display, const char* text, int y) {
    int textWidth = 0;
    const char* p = text;
    while (*p++) textWidth++;
    textWidth *= 6;
    int x = (128 - textWidth) / 2;
    display->drawText(text, x, y);
}

} // namespace BoldRetroUI
