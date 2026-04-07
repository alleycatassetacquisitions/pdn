#include "symbol-match/symbol-manager.hpp"

SymbolManager::SymbolManager() {
    symbols[SymbolPosition::LEFT] = new Symbol();
    symbols[SymbolPosition::RIGHT] = new Symbol();
}

SymbolManager::~SymbolManager() {
    for(auto& symbol : symbols) {
        delete symbol.second;
    }
    symbols.clear();
}

void SymbolManager::refreshSymbols() {
    for(auto& symbol : symbols) {
        symbol.second->setRandomSymbol();
    }
}

Symbol* SymbolManager::getSymbol(SymbolPosition position) {
    return symbols[position];
}

const char* SymbolManager::getSymbolGlyph(SymbolPosition position) {
    return symbols[position]->getSymbolGlyph();
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

void SymbolManager::setLeftMatched(bool matched) {
    leftMatched = matched;
}

void SymbolManager::setRightMatched(bool matched) {
    rightMatched = matched;
}

bool SymbolManager::isLeftMatched() {
    return leftMatched;
}

bool SymbolManager::isRightMatched() {
    return rightMatched;
}