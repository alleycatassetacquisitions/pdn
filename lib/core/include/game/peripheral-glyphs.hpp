#pragma once

#include "symbol.hpp"

enum class PeripheralGlyphId : uint8_t {
    SCORING = static_cast<uint8_t>(SymbolId::NUM_SYMBOLS),
};

namespace PeripheralGlyphs {

inline const char* scoring() { return "\u00f5"; }

inline const char* glyphForId(uint8_t id) {
    if (id == static_cast<uint8_t>(PeripheralGlyphId::SCORING)) {
        return scoring();
    }
    return nullptr;
}

}  // namespace PeripheralGlyphs
