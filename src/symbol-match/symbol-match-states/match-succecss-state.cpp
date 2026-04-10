#include "symbol-match/symbol-match-states.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatch";


MatchSuccess::MatchSuccess(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
    SymbolWirelessManager* symbolWirelessManager) : ConnectState(remoteDeviceCoordinator, MATCH_SUCCESS) {
    this->symbolManager = symbolManager;
    this->symbolWirelessManager = symbolWirelessManager;
}

MatchSuccess::~MatchSuccess() {
    symbolManager = nullptr;
    symbolWirelessManager = nullptr;
}

void MatchSuccess::onStateMounted(Device *FDN) {
    LOG_W(TAG, "MatchSuccess mounted");
    bufferTimer.setTimer(bufferInterval);
    renderTimer.setTimer(renderInterval);

    // Send SYMBOL_MATCH_SUCCESS to all known peers
    for (SerialIdentifier port : {SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac != nullptr) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SYMBOL_MATCH_SUCCESS, SymbolId::SYMBOL_A, port);
        }
    }
}

void MatchSuccess::onStateLoop(Device *FDN) {
    if (bufferTimer.expired()) {
        transitionToSelectionState = true;
    }
    
    if (renderTimer.expired()) {
        renderSymbolScreen(FDN);
        renderTimer.setTimer(renderInterval);
    }
}

void MatchSuccess::onStateDismounted(Device *FDN) {
    LOG_W(TAG, "MatchSuccess dismounted");
    bufferTimer.invalidate();
    renderTimer.invalidate();
    transitionToSelectionState = false;
    toggleBlink = true;
}

void MatchSuccess::renderSymbolScreen(Device *FDN) {
    FDN->getDisplay()->invalidateScreen();
    FDN->getDisplay()->whiteScreen();
    FDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (toggleBlink) {
        FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::LEFT), 24, 40);
        FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::RIGHT), 72, 40);
    }
    FDN->getDisplay()->render();

    toggleBlink = !toggleBlink;
}

bool MatchSuccess::transitionToSelection() {
    return transitionToSelectionState;
}

bool MatchSuccess::isPrimaryRequired() {
    return true;
}

bool MatchSuccess::isAuxRequired() {
    return true;
}