#include "device/remote-device-coordinator.hpp"
#include "symbol-match/symbol-match-states.hpp"
#include "device/device.hpp"

SymbolIdle::SymbolIdle(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) : ConnectState(remoteDeviceCoordinator, SYMBOL_IDLE) {
    this->symbolManager = symbolManager;
}

SymbolIdle::~SymbolIdle() {
}

void SymbolIdle::onStateMounted(Device *FDN) {
    refreshTimer.setTimer(refreshInterval);

    renderSymbolScreen(FDN);
}

void SymbolIdle::onStateLoop(Device *FDN) {
    if (refreshTimer.expired()) {
        transitionToSelectionState = true;
    } else if (refreshTimer.isRunning()) {
        if (refreshTimer.getElapsedTime() - lastTimeRendered >= 1000) {
            lastTimeRendered = refreshTimer.getElapsedTime();
            renderSymbolScreen(FDN);
        }
    }
}

void SymbolIdle::onStateDismounted(Device *FDN) {
    refreshTimer.invalidate();
    transitionToSelectionState = false;
}

void SymbolIdle::renderSymbolScreen(Device *FDN) {
    FDN->getDisplay()->invalidateScreen();

    renderSymbolGlyphs(FDN);
    renderTimer(FDN);

    FDN->getDisplay()->render();
}

void SymbolIdle::renderSymbolGlyphs(Device *FDN) {
    FDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::LEFT), 24, 40);
    FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::RIGHT), 72, 40);
}

void SymbolIdle::renderTimer(Device *FDN) {
    FDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    int timeLeft = refreshInterval - refreshTimer.getElapsedTime();
    
    int minutes = timeLeft / 60000;
    int seconds = (timeLeft % 60000) / 1000;

    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    FDN->getDisplay()->drawText(buffer, 40, 64);
}

bool SymbolIdle::transitionToSelection() {
    return transitionToSelectionState;
}

bool SymbolIdle::isPrimaryRequired() {
    return true;
}

bool SymbolIdle::isAuxRequired() {
    return true;
}
