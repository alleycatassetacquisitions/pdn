#include "apps/symbol-match/symbol-manager.hpp"

SymbolManager::SymbolManager() {
    symbols[SerialIdentifier::INPUT_JACK] = new Symbol();
    symbols[SerialIdentifier::INPUT_JACK_SECONDARY] = new Symbol();
    matchedByPort[SerialIdentifier::INPUT_JACK] = false;
    matchedByPort[SerialIdentifier::INPUT_JACK_SECONDARY] = false;

    std::srand(35);
    refreshSymbols();
}

SymbolManager::~SymbolManager() {
    for (auto& symbol : symbols) {
        delete symbol.second;
    }
    symbols.clear();
}

void SymbolManager::refreshSymbols() {
    const SymbolId prevLeft  = symbols[SerialIdentifier::INPUT_JACK]->getSymbolId();
    const SymbolId prevRight = symbols[SerialIdentifier::INPUT_JACK_SECONDARY]->getSymbolId();
    do {
        symbols[SerialIdentifier::INPUT_JACK]->setRandomSymbol();
    } while (symbols[SerialIdentifier::INPUT_JACK]->getSymbolId() == prevLeft
          || symbols[SerialIdentifier::INPUT_JACK]->getSymbolId() == prevRight);
    do {
        symbols[SerialIdentifier::INPUT_JACK_SECONDARY]->setRandomSymbol();
    } while (symbols[SerialIdentifier::INPUT_JACK_SECONDARY]->getSymbolId() == prevLeft
          || symbols[SerialIdentifier::INPUT_JACK_SECONDARY]->getSymbolId() == prevRight);
}

Symbol* SymbolManager::getSymbol(SerialIdentifier port) {
    return symbols[port];
}

const char* SymbolManager::getSymbolGlyph(SerialIdentifier port) {
    return symbols[port]->getSymbolGlyph();
}

SimpleTimer* SymbolManager::getRefreshTimer() {
    return &refreshTimer;
}

int SymbolManager::getTimeLeftToRefresh() {
    return refreshInterval - refreshTimer.getElapsedTime();
}

void SymbolManager::resetRefreshTimer() {
    refreshTimer.setTimer(refreshInterval);
}

void SymbolManager::setMatched(const SerialIdentifier port, const bool matched) {
    matchedByPort[port] = matched;
}

bool SymbolManager::isMatched(const SerialIdentifier port) const {
    const auto it = matchedByPort.find(port);
    return it != matchedByPort.end() && it->second;
}
