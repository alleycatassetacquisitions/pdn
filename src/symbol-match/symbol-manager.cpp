#include "symbol-match/symbol-manager.hpp"

SymbolManager::SymbolManager() {
    refreshSymbols();

    symbolGlyphMap[Symbol::SYMBOL_A] = "\u0089";
    symbolGlyphMap[Symbol::SYMBOL_B] = "\u0103";
    symbolGlyphMap[Symbol::SYMBOL_C] = "\u0107";
}

SymbolManager::~SymbolManager() {

}

void SymbolManager::refreshSymbols() {
    leftSymbol = static_cast<Symbol>(std::rand() % (int)Symbol::NUM_SYMBOLS);
    rightSymbol = static_cast<Symbol>(std::rand() % (int)Symbol::NUM_SYMBOLS);
}

Symbol SymbolManager::getLeftSymbol() {
    return leftSymbol;
}

Symbol SymbolManager::getRightSymbol() {
    return rightSymbol;
}

const char* SymbolManager::symbolToGlyph(Symbol symbol) {
    return symbolGlyphMap[symbol];
}