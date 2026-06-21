#pragma once

#include <map>
#include "symbol.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "utils/simple-timer.hpp"

class SymbolLockManager {
public:
    explicit SymbolLockManager(bool singleSymbolMode);
    ~SymbolLockManager();

    bool isSingleSymbolMode() const;

    Symbol* getSymbol(SerialIdentifier port);
    const char* getSymbolGlyph(SerialIdentifier port);
    void refreshSymbols();

    void resetRefreshTimer();
    int getTimeLeftToRefresh();
    SimpleTimer* getRefreshTimer();

    bool isMatched(SerialIdentifier port) const;
    void setMatched(SerialIdentifier port, bool matched);

    bool allConnectedPortsMatched(bool leftConnected, bool rightConnected) const;

private:
    bool singleSymbolMode;
    std::map<SerialIdentifier, Symbol*> symbols;
    std::map<SerialIdentifier, bool> matchedByPort;

    SimpleTimer refreshTimer;
    int refreshInterval = 30 * 60 * 1000;
};
