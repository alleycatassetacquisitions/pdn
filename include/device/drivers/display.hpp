#pragma once

#include "image.hpp"
#include "display-types.hpp"


class Display {
public:
    virtual ~Display() = default;

    virtual Display* invalidateScreen() = 0;

    virtual void render() = 0;

    virtual Display* drawText(const char *text, bool centered = false) = 0;

    virtual Display* setGlyphMode(FontMode mode) = 0;

    virtual Display* renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) = 0;

    virtual Display* drawButton(const char *text, int xCenter, int yCenter) = 0;

    virtual Display* drawText(const char *text, int xStart, int yStart, bool centered = false) = 0;

    virtual Display* drawImage(Image image) = 0;

    virtual Display* drawImage(Image image, int xStart, int yStart) = 0;

    virtual Display* drawShape(const Shape& shape) = 0;

private:
    Cursor cursor_;
};
