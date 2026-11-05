#include "apps/symbol-match/symbol-manager.hpp"

SymbolManager::SymbolManager() {
    symbols[SerialIdentifier::INPUT_JACK] = new Symbol();
    symbols[SerialIdentifier::INPUT_JACK_SECONDARY] = new Symbol();
    matchedByJack[SerialIdentifier::INPUT_JACK] = false;
    matchedByJack[SerialIdentifier::INPUT_JACK_SECONDARY] = false;

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

Symbol* SymbolManager::getSymbol(SerialIdentifier jack) {
    return symbols[jack];
}

const char* SymbolManager::getSymbolGlyph(SerialIdentifier jack) {
    return symbols[jack]->getSymbolGlyph();
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

void SymbolManager::setMatched(const SerialIdentifier jack, const bool matched) {
    matchedByJack[jack] = matched;
}

bool SymbolManager::isMatched(const SerialIdentifier jack) const {
    const auto it = matchedByJack.find(jack);
    return it != matchedByJack.end() && it->second;
}
