#pragma once

#include <random>
#include <map>

enum class Symbol {
    SYMBOL_A = 0,
    SYMBOL_B,
    SYMBOL_C,
    NUM_SYMBOLS,    // add new symbols above this line
};

class SymbolManager {
    public:
    SymbolManager();
    ~SymbolManager();

    void refreshSymbols();

    Symbol getLeftSymbol();
    Symbol getRightSymbol();

    const char* symbolToGlyph(Symbol symbol);

private:
    Symbol leftSymbol;
    Symbol rightSymbol;

    std::map<Symbol, const char*> symbolGlyphMap;
};