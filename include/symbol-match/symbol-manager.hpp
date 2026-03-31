#pragma once

#include <random>
#include <map>
#include "symbol-match/symbol.hpp"

enum class SymbolPosition {
    LEFT = 0,
    RIGHT = 1,
};

class SymbolManager {
    public:
    SymbolManager();
    ~SymbolManager();

    Symbol* getSymbol(SymbolPosition position);
    const char* getSymbolGlyph(SymbolPosition position);
    void refreshSymbols();

private:
    std::map<SymbolPosition, Symbol*> symbols;
};