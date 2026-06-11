#include "apps/symbol-match/symbol-match-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "Selection";

Selection::Selection(SymbolManager* symbolManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator,
                     SymbolWirelessManager* symbolWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, SELECTION)
    , symbolManager(symbolManager)
    , symbolWirelessManager(symbolWirelessManager) {}

Selection::~Selection() {
    symbolManager = nullptr;
    symbolWirelessManager = nullptr;
}

void Selection::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    symbolManager->getRefreshTimer()->invalidate();
    symbolManager->refreshSymbols();
    bufferTimer.setTimer(bufferInterval);
}

void Selection::onStateLoop(FDN* fdn) {
    if (bufferTimer.expired()) {
        transitionToIdleState = true;
    } else if (bufferTimer.isRunning()) {
        showLoadingGlyphs(fdn->getDisplay());
    }
}

void Selection::onStateDismounted(FDN* fdn) {
    bufferTimer.invalidate();
    transitionToIdleState = false;
    fdn->getDisplay()->invalidateScreen();
    symbolManager->resetRefreshTimer();
    LOG_W(TAG, "Dismounted");
}

bool Selection::transitionToIdle() {
    return transitionToIdleState;
}
