#include "symbol-match/symbol-match-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatch";

BothConnected::BothConnected(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) : ConnectState(remoteDeviceCoordinator, BOTH_CONNECTED) {
    this->symbolManager = symbolManager;
}

BothConnected::~BothConnected() {
}

void BothConnected::onStateMounted(Device *FDN) {
    LOG_W(TAG, "BothConnected mounted");
}

void BothConnected::onStateLoop(Device *FDN) {
    if (symbolManager->getRefreshTimer()->expired()) {
        transitionToSelectionState = true;
        LOG_W(TAG, "BothConnected: transitionToSelectionState = true");
    } else if (remoteDeviceCoordinator->getPortStatus(SerialIdentifier::OUTPUT_JACK) == PortStatus::DISCONNECTED) {
        symbolManager->setLeftMatched(false);
        transitionToRightConnectedState = true;
    } else if (remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::DISCONNECTED) {
        symbolManager->setRightMatched(false);
        transitionToLeftConnectedState = true;
    } else if (symbolManager->getRefreshTimer()->isRunning()) {
        int elapsedTime = symbolManager->getRefreshTimer()->getElapsedTime();
        if (elapsedTime - lastTimeRendered >= 500) {
            toggleBlink = !toggleBlink;
            lastTimeRendered = elapsedTime;
            renderSymbolScreen(FDN);
        }
    }
}

void BothConnected::onStateDismounted(Device *FDN) {
    transitionToSelectionState = false;
    transitionToLeftConnectedState = false;
    transitionToRightConnectedState = false;
    transitionToMatchSuccessState = false;
    LOG_W(TAG, "BothConnected dismounted");

    lastTimeRendered = 0;
}

void BothConnected::renderSymbolScreen(Device *FDN) {
    FDN->getDisplay()->invalidateScreen();

    // render symbol glyphs
    FDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (toggleBlink) {
        FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::LEFT), 24, 40);
        FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::RIGHT), 72, 40);
    } 

    // render timer
    FDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    int timeLeft = symbolManager->getTimeLeftToRefresh();
    
    int minutes = timeLeft / 60000;
    int seconds = (timeLeft % 60000) / 1000;

    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    FDN->getDisplay()->drawText(buffer, 40, 64);

    FDN->getDisplay()->render();
}

bool BothConnected::transitionToSelection() {
    return transitionToSelectionState;
}

bool BothConnected::transitionToMatchSuccess() {
    return transitionToMatchSuccessState;
}

bool BothConnected::transitionToLeftConnected() {
    return transitionToLeftConnectedState;
}

bool BothConnected::transitionToRightConnected() {
    return transitionToRightConnectedState;
}

bool BothConnected::isPrimaryRequired() {
    return true;
}

bool BothConnected::isAuxRequired() {
    return true;
}