#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatched";

SymbolMatched::SymbolMatched(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager) : ConnectState(remoteDeviceCoordinator, SYMBOL_MATCHED) {
    this->player = player;
    this->symbolWirelessManager = symbolWirelessManager;
}

SymbolMatched::~SymbolMatched() {
    this->player = nullptr;
}

void SymbolMatched::onStateMounted(Device *PDN) {
    LOG_W(TAG, "mounted");
    transitionToSymbolState = false;
    toggleBlink = true;
    blinkTimer.setTimer(BLINK_INTERVAL);

    symbolWirelessManager->setPacketReceivedCallback(std::bind(&SymbolMatched::onSymbolMatchCommandReceived, this, std::placeholders::_1));
}

void SymbolMatched::onStateLoop(Device *PDN) {
    if (blinkTimer.expired()) {
        renderSymbolScreen(PDN);
        blinkTimer.setTimer(BLINK_INTERVAL);
    }
}

void SymbolMatched::onStateDismounted(Device *PDN) {
    LOG_W(TAG, "dismounted");
    transitionToSymbolState = false;
    toggleBlink = true;
    blinkTimer.invalidate();
    symbolWirelessManager->clearCallback();
}

bool SymbolMatched::transitionToSymbol() {
    return transitionToSymbolState;
}

void SymbolMatched::renderSymbolScreen(Device *PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->whiteScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (toggleBlink) {
        PDN->getDisplay()->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);
    }
    PDN->getDisplay()->render();
    toggleBlink = !toggleBlink;
}

void SymbolMatched::onSymbolMatchCommandReceived(SymbolMatchCommand command) {
    if (command.command == SMCommand::SYMBOLS_REFRESHED) {
        transitionToSymbolState = true;
    }
}

bool SymbolMatched::isPrimaryRequired() {
    return true;
}

bool SymbolMatched::isAuxRequired() {
    return true;
}