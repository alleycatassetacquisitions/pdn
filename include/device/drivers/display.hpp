#pragma once

#include "image.hpp"

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

    // Pixel width of the given text in the current font/glyph mode.
    virtual int getTextWidth(const char* text) = 0;

    virtual int getWidth() = 0;

    Display* drawCenteredText(const char* text, int y) {
        drawText(text, (getWidth() - getTextWidth(text)) / 2, y);
        return this;
    }

private:
    Cursor cursor_;
};
