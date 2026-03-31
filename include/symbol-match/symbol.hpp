#pragma once

#include <random>
#include <map>

enum class SymbolId {
    SYMBOL_A = 0,
    SYMBOL_B,
    SYMBOL_C,
    NUM_SYMBOLS
};

class Symbol {
public:
    Symbol();
    ~Symbol();

    void setRandomSymbol();
    SymbolId getSymbolId();
    const char* getSymbolGlyph();

private:
    SymbolId symbolId;

    std::map<SymbolId, const char*> symbolGlyphMap;
};