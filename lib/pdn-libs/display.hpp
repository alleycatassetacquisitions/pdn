#pragma once

#include "image.hpp"

struct Cursor {
    int x = 0;
    int y = 0;
};

enum class FontMode {
    TEXT,
    NUMBER_GLYPH,
    LOADING_GLYPH
};

class Display {
public:

    virtual ~Display() {}

    virtual Display* invalidateScreen() = 0;

    virtual void render() = 0;

    virtual Display* drawText(const char *text) = 0;

    virtual Display* setGlyphMode(FontMode mode) = 0;

    virtual Display* renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) = 0;

    virtual Display* drawButton(const char *text, int xCenter, int yCenter) = 0;

    virtual Display* drawText(const char *text, int xStart, int yStart) = 0;

    virtual Display* drawImage(Image image) = 0;

    virtual Display* drawImage(Image image, int xStart, int yStart) = 0;

protected:
    Cursor cursor;
};
