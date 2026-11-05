#pragma once

#include <map>
#include "symbol.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "utils/simple-timer.hpp"

class SymbolManager {
public:
    SymbolManager();
    ~SymbolManager();

    /** The symbol currently shown for the given jack. */
    Symbol* getSymbol(SerialIdentifier jack);
    /** The glyph for that jack's symbol, ready to draw. */
    const char* getSymbolGlyph(SerialIdentifier jack);
    void refreshSymbols();

    void resetRefreshTimer();
    int getTimeLeftToRefresh();
    SimpleTimer* getRefreshTimer();

    /** True once the peer on this jack has answered with the same symbol. */
    bool isMatched(SerialIdentifier jack) const;
    /** Records whether this jack's symbol has been matched. */
    void setMatched(SerialIdentifier jack, bool matched);

private:
    std::map<SerialIdentifier, Symbol*> symbols;
    std::map<SerialIdentifier, bool> matchedByJack;

    SimpleTimer refreshTimer;
    int refreshInterval = 30 * 60 * 1000;
};
