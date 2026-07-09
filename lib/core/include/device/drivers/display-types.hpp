#pragma once

#include "image.hpp"

constexpr int DISPLAY_INSTRUCTION_CAPACITY = 20;
constexpr int DISPLAY_TEXT_CAPACITY = 32;

enum class Anchor {
    LEADING,
    CENTER,
    TRAILING,
};

enum class DrawType {
    TEXT,
    GLYPH,
    IMAGE,
};

struct Cursor {
    int x = 0;
    int y = 0;
};

enum class FontMode {
    TEXT,
    NUMBER_GLYPH,
    LOADING_GLYPH,
    TEXT_INVERTED_SMALL,
    TEXT_INVERTED_LARGE,
    SYMBOL_GLYPH
};

struct DrawCoordinates {
    int x = 0;
    int y = 0;
    Anchor xAnchor = Anchor::LEADING;
    Anchor yAnchor = Anchor::LEADING;
};

struct ImagePayload {
    Image image;
    DrawCoordinates coordinates;
};

struct TextPayload {
    const char* text;
    DrawCoordinates coordinates;
    FontMode fontMode;
};

struct GlyphPayload {
    const char* unicodeForGlyph;
    DrawCoordinates coordinates;
};

struct DrawInstruction {
    DrawType drawType;
    union {
        ImagePayload imagePayload;
        TextPayload textPayload;
        GlyphPayload glyphPayload;
    };
};