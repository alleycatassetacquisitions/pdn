#include "apps/symbol-lock/symbol-lock-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolLockSelection";

SymbolLockSelectionState::SymbolLockSelectionState(
    SymbolLockManager* symbolManager,
    RemoteDeviceCoordinator* remoteDeviceCoordinator,
    SymbolWirelessManager* symbolWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, SymbolLockStateId::SYMBOL_LOCK_SELECTION)
    , symbolManager(symbolManager)
    , symbolWirelessManager(symbolWirelessManager) {}

SymbolLockSelectionState::~SymbolLockSelectionState() {
    symbolManager = nullptr;
    symbolWirelessManager = nullptr;
}

void SymbolLockSelectionState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    symbolManager->getRefreshTimer()->invalidate();
    symbolManager->refreshSymbols();
    bufferTimer.setTimer(bufferInterval);
}

void SymbolLockSelectionState::onStateLoop(FDN* fdn) {
    if (bufferTimer.expired()) {
        transitionToIdleState = true;
    } else if (bufferTimer.isRunning()) {
        showLoadingGlyphs(fdn->getDisplay());
    }
}

void SymbolLockSelectionState::onStateDismounted(FDN* fdn) {
    bufferTimer.invalidate();
    transitionToIdleState = false;
    fdn->getDisplay()->invalidateScreen();
    symbolManager->resetRefreshTimer();
    LOG_W(TAG, "Dismounted");
}

bool SymbolLockSelectionState::transitionToIdle() {
    return transitionToIdleState;
}
