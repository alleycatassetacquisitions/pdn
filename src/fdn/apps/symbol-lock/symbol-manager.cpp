#include "apps/symbol-lock/symbol-manager.hpp"

SymbolLockManager::SymbolLockManager(bool singleSymbolMode)
    : singleSymbolMode(singleSymbolMode) {
    symbols[SerialIdentifier::INPUT_JACK] = new Symbol();
    symbols[SerialIdentifier::INPUT_JACK_SECONDARY] = new Symbol();
    matchedByPort[SerialIdentifier::INPUT_JACK] = false;
    matchedByPort[SerialIdentifier::INPUT_JACK_SECONDARY] = false;

    std::srand(35);
    refreshSymbols();
}

SymbolLockManager::~SymbolLockManager() {
    for (auto& symbol : symbols) {
        delete symbol.second;
    }
    symbols.clear();
}

bool SymbolLockManager::isSingleSymbolMode() const {
    return singleSymbolMode;
}

void SymbolLockManager::refreshSymbols() {
    if (singleSymbolMode) {
        symbols[SerialIdentifier::INPUT_JACK]->setRandomSymbol();
        const SymbolId shared = symbols[SerialIdentifier::INPUT_JACK]->getSymbolId();
        symbols[SerialIdentifier::INPUT_JACK_SECONDARY]->setRandomSymbol();
        while (symbols[SerialIdentifier::INPUT_JACK_SECONDARY]->getSymbolId() != shared) {
            symbols[SerialIdentifier::INPUT_JACK_SECONDARY]->setRandomSymbol();
        }
        return;
    }

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

Symbol* SymbolLockManager::getSymbol(SerialIdentifier port) {
    return symbols[port];
}

const char* SymbolLockManager::getSymbolGlyph(SerialIdentifier port) {
    if (singleSymbolMode) {
        return symbols[SerialIdentifier::INPUT_JACK]->getSymbolGlyph();
    }
    return symbols[port]->getSymbolGlyph();
}

SimpleTimer* SymbolLockManager::getRefreshTimer() {
    return &refreshTimer;
}

int SymbolLockManager::getTimeLeftToRefresh() {
    return refreshInterval - refreshTimer.getElapsedTime();
}

void SymbolLockManager::resetRefreshTimer() {
    refreshTimer.setTimer(refreshInterval);
}

void SymbolLockManager::setMatched(const SerialIdentifier port, const bool matched) {
    matchedByPort[port] = matched;
}

bool SymbolLockManager::isMatched(const SerialIdentifier port) const {
    const auto it = matchedByPort.find(port);
    return it != matchedByPort.end() && it->second;
}

bool SymbolLockManager::allConnectedPortsMatched(bool leftConnected, bool rightConnected) const {
    if (!leftConnected && !rightConnected) {
        return false;
    }
    if (leftConnected && !isMatched(SerialIdentifier::INPUT_JACK)) {
        return false;
    }
    if (rightConnected && !isMatched(SerialIdentifier::INPUT_JACK_SECONDARY)) {
        return false;
    }
    return true;
}
