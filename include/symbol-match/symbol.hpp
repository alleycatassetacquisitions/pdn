#pragma once

#include <string>
#include <random>
#include <map>

enum class SymbolId {
    SYMBOL_A = 0,
    SYMBOL_B,
    SYMBOL_C,
    SYMBOL_D,
    SYMBOL_E,
    NUM_SYMBOLS
};

class Symbol {
public:
    Symbol();
    ~Symbol();

    void setRandomSymbol();
    void updateFromUserIdString(const std::string& userId);
    SymbolId getSymbolId();
    const char* getSymbolGlyph();

private:
    SymbolId symbolId;

    std::map<SymbolId, const char*> symbolGlyphMap;
};