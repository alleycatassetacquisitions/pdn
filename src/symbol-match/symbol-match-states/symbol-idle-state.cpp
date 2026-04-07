#include "device/remote-device-coordinator.hpp"
#include "symbol-match/symbol-match-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatch";

SymbolIdle::SymbolIdle(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) : ConnectState(remoteDeviceCoordinator, SYMBOL_IDLE) {
    this->symbolManager = symbolManager;
}

SymbolIdle::~SymbolIdle() {
}

void SymbolIdle::onStateMounted(Device *FDN) {
    LOG_W(TAG, "SymbolIdle mounted");

    renderSymbolScreen(FDN);

    transitionToRightConnectedState = false;
}

void SymbolIdle::onStateLoop(Device *FDN) {
    if (symbolManager->getRefreshTimer()->expired()) {
        transitionToSelectionState = true;
        LOG_W(TAG, "SymbolIdle: transitionToSelectionState = true");
    } else if (remoteDeviceCoordinator->getPortStatus(SerialIdentifier::OUTPUT_JACK) == PortStatus::CONNECTED) {  // left port
        transitionToLeftConnectedState = true;
    } else if (remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED) {  // right port
        transitionToRightConnectedState = true;
    } else if (symbolManager->getRefreshTimer()->isRunning()) {
        int elapsedTime = symbolManager->getRefreshTimer()->getElapsedTime();
        if (elapsedTime - lastTimeRendered >= 1000) {
            lastTimeRendered = elapsedTime;
            renderSymbolScreen(FDN);
        }
    }
}

void SymbolIdle::onStateDismounted(Device *FDN) {
    transitionToSelectionState = false;
    transitionToLeftConnectedState = false;
    transitionToRightConnectedState = false;
    LOG_W(TAG, "SymbolIdle dismounted");

    lastTimeRendered = 0;
}

void SymbolIdle::renderSymbolScreen(Device *FDN) {
    FDN->getDisplay()->invalidateScreen();

    // render symbol glyphs
    FDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::LEFT), 24, 40);
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

bool SymbolIdle::transitionToSelection() {
    return transitionToSelectionState;
}

bool SymbolIdle::transitionToLeftConnected() {
    return transitionToLeftConnectedState;
}

bool SymbolIdle::transitionToRightConnected() {
    return transitionToRightConnectedState;
}

bool SymbolIdle::isPrimaryRequired() {
    return true;
}

bool SymbolIdle::isAuxRequired() {
    return true;
}
