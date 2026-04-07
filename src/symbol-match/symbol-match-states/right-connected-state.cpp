#include "symbol-match/symbol-match-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatch";

RightConnected::RightConnected(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) : ConnectState(remoteDeviceCoordinator, RIGHT_CONNECTED) {
    this->symbolManager = symbolManager;
}

RightConnected::~RightConnected() {
}

void RightConnected::onStateMounted(Device *FDN) {
    LOG_W(TAG, "RightConnected mounted");
}

void RightConnected::onStateLoop(Device *FDN) {
    if (symbolManager->getRefreshTimer()->expired()) {
        transitionToSelectionState = true;
        LOG_W(TAG, "RightConnected: transitionToSelectionState = true");
    } else if (remoteDeviceCoordinator->getPortStatus(SerialIdentifier::OUTPUT_JACK) == PortStatus::CONNECTED) {  // left port
        transitionToBothConnectedState = true;
    } else if (remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::DISCONNECTED) {
        symbolManager->setRightMatched(false);
        transitionToSymbolIdleState = true;
    } else if (symbolManager->getRefreshTimer()->isRunning()) {
        int elapsedTime = symbolManager->getRefreshTimer()->getElapsedTime();
        if (elapsedTime - lastTimeRendered >= 500) {
            toggleBlink = !toggleBlink;
            lastTimeRendered = elapsedTime;
            renderSymbolScreen(FDN);
        }
    }

    
}

void RightConnected::onStateDismounted(Device *FDN) {
    transitionToSelectionState = false;
    transitionToSymbolIdleState = false;
    transitionToBothConnectedState = false;
    LOG_W(TAG, "RightConnected dismounted");

    lastTimeRendered = 0;
}

void RightConnected::renderSymbolScreen(Device *FDN) {
    FDN->getDisplay()->invalidateScreen();

    // render symbol glyphs
    FDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (toggleBlink) {
        FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::RIGHT), 72, 40);
    } 
    FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::LEFT), 24, 40);

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

bool RightConnected::transitionToSymbolIdle() {
    return transitionToSymbolIdleState;
}

bool RightConnected::transitionToBothConnected() {
    return transitionToBothConnectedState;
}

bool RightConnected::transitionToSelection() {
    return transitionToSelectionState;
}

bool RightConnected::isPrimaryRequired() {
    return true;
}

bool RightConnected::isAuxRequired() {
    return true;
}