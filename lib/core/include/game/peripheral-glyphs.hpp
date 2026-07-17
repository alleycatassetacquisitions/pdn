#pragma once

#include "symbol.hpp"

enum class PeripheralGlyphId : uint8_t {
    SCORING = static_cast<uint8_t>(SymbolId::NUM_SYMBOLS),
    ARROW_UP,
    ARROW_LEFT,
    ARROW_DOWN,
    ARROW_RIGHT,
};

namespace PeripheralGlyphs {

// Open Iconic codes for u8g2_font_open_iconic_all_4x_t (FontMode::SYMBOL_GLYPH).
inline const char* scoring() { return "\u00f5"; }
inline const char* arrowUp() { return "\u0077"; }
inline const char* arrowLeft() { return "\u0075"; }
inline const char* arrowDown() { return "\u0074"; }
inline const char* arrowRight() { return "\u0076"; }

inline const char* glyphForId(uint8_t id) {
    switch (static_cast<PeripheralGlyphId>(id)) {
        case PeripheralGlyphId::SCORING:
            return scoring();
        case PeripheralGlyphId::ARROW_UP:
            return arrowUp();
        case PeripheralGlyphId::ARROW_LEFT:
            return arrowLeft();
        case PeripheralGlyphId::ARROW_DOWN:
            return arrowDown();
        case PeripheralGlyphId::ARROW_RIGHT:
            return arrowRight();
        default:
            return nullptr;
    }
}

}  // namespace PeripheralGlyphs
