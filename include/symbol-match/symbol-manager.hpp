#pragma once

#include <random>
#include <map>
#include "symbol-match/symbol.hpp"
#include "utils/simple-timer.hpp"

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

    void validateSymbols(const uint8_t* fdnMac);
    
    void resetRefreshTimer();
    int getTimeLeftToRefresh();
    SimpleTimer* getRefreshTimer();

    bool isLeftMatched();
    bool isRightMatched();
    void setLeftMatched(bool matched);
    void setRightMatched(bool matched);

private:
    std::map<SymbolPosition, Symbol*> symbols;

    SimpleTimer refreshTimer;
    int refreshInterval = (int)(30 * 1000);

    bool leftMatched = false;
    bool rightMatched = false;
};