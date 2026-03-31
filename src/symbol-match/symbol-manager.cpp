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