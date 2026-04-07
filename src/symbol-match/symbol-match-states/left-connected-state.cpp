#include "symbol-match/symbol-match-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatch";

LeftConnected::LeftConnected(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) : ConnectState(remoteDeviceCoordinator, LEFT_CONNECTED) {
    this->symbolManager = symbolManager;
}

LeftConnected::~LeftConnected() {
}

void LeftConnected::onStateMounted(Device *FDN) {
    LOG_W(TAG, "LeftConnected mounted");
}

void LeftConnected::onStateLoop(Device *FDN) {
    if (symbolManager->getRefreshTimer()->expired()) {
        transitionToSelectionState = true;
        LOG_W(TAG, "LeftConnected: transitionToSelectionState = true");
    } else if (remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED) {  // right port
        transitionToBothConnectedState = true;
    } else if (remoteDeviceCoordinator->getPortStatus(SerialIdentifier::OUTPUT_JACK) == PortStatus::DISCONNECTED) {
        symbolManager->setLeftMatched(false);
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

void LeftConnected::onStateDismounted(Device *FDN) {
    transitionToSelectionState = false;
    transitionToSymbolIdleState = false;
    transitionToBothConnectedState = false;
    LOG_W(TAG, "LeftConnected dismounted");

    lastTimeRendered = 0;
}

void LeftConnected::renderSymbolScreen(Device *FDN) {
    FDN->getDisplay()->invalidateScreen();

    // render symbol glyphs (mirror RightConnected: opposite side blinks — left port held, blink RIGHT)
    FDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (toggleBlink) {
        FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::LEFT), 24, 40);
    }
    FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::RIGHT), 72, 40);

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

bool LeftConnected::transitionToSymbolIdle() {
    return transitionToSymbolIdleState;
}

bool LeftConnected::transitionToBothConnected() {
    return transitionToBothConnectedState;
}

bool LeftConnected::transitionToSelection() {
    return transitionToSelectionState;
}

bool LeftConnected::isPrimaryRequired() {
    return true;
}

bool LeftConnected::isAuxRequired() {
    return true;
}