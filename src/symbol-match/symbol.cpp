#include "symbol-match/symbol.hpp"
#include <string>

Symbol::Symbol() {
    setRandomSymbol();

    symbolGlyphMap[SymbolId::SYMBOL_A] = "\u0089";
    symbolGlyphMap[SymbolId::SYMBOL_B] = "\u0103";
    symbolGlyphMap[SymbolId::SYMBOL_C] = "\u0107";
    symbolGlyphMap[SymbolId::SYMBOL_D] = "\u0081";
    symbolGlyphMap[SymbolId::SYMBOL_E] = "\u0047";
}

Symbol::~Symbol() {

}

void Symbol::setRandomSymbol() {
    symbolId = static_cast<SymbolId>(std::rand() % (int)SymbolId::NUM_SYMBOLS);
}

void Symbol::updateFromUserIdString(const std::string& userId) {
    if (userId.empty()) {
        return;
    }
    try {
        long n = std::stol(userId);
        symbolId = static_cast<SymbolId>(n % (int)SymbolId::NUM_SYMBOLS);
    } catch (...) {
        size_t h = 0;
        for (char c : userId) {
            h = h * 131U + static_cast<unsigned char>(c);
        }
        symbolId = static_cast<SymbolId>(h % (int)SymbolId::NUM_SYMBOLS);
    }
}

SymbolId Symbol::getSymbolId() {
    return this->symbolId;
}

const char* Symbol::getSymbolGlyph() {
    return symbolGlyphMap[symbolId];
}