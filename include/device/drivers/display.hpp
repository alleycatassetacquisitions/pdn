#pragma once

#include "image.hpp"
#include <cstdint>

struct Cursor {
    int x = 0;
    int y = 0;
};

enum class FontMode {
    TEXT,
    NUMBER_GLYPH,
    LOADING_GLYPH,
    TEXT_INVERTED_SMALL,
    TEXT_INVERTED_LARGE
};

class Display {
public:
    virtual ~Display() = default;

    virtual Display* invalidateScreen() = 0;

    virtual void render() = 0;

    virtual Display* drawText(const char *text) = 0;

    virtual Display* setGlyphMode(FontMode mode) = 0;

    virtual Display* renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) = 0;

    virtual Display* drawButton(const char *text, int xCenter, int yCenter) = 0;

    virtual Display* drawText(const char *text, int xStart, int yStart) = 0;

    virtual Display* drawImage(Image image) = 0;

    virtual Display* drawImage(Image image, int xStart, int yStart) = 0;

    virtual Display* drawBox(int x, int y, int w, int h) = 0;

    virtual Display* drawFrame(int x, int y, int w, int h) = 0;

    virtual Display* drawGlyph(int x, int y, uint16_t glyph) = 0;

    virtual Display* setDrawColor(int color) = 0;

    virtual Display* setFont(const uint8_t *font) = 0;

private:
    Cursor cursor_;
};
