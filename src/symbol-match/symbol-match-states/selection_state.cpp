#include "symbol-match/symbol-match-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatch";

Selection::Selection(SymbolManager* symbolManager) : State(SELECTION) {
    this->symbolManager = symbolManager;
}

Selection::~Selection() {
}

void Selection::onStateMounted(Device *FDN) {
    LOG_W(TAG, "Selection mounted");
    symbolManager->getRefreshTimer()->invalidate();
    symbolManager->refreshSymbols();
    bufferTimer.setTimer(bufferInterval);
}

void Selection::onStateLoop(Device *FDN) {
    if (bufferTimer.expired()) {
        transitionToIdleState = true;
    } else if (bufferTimer.isRunning()) {
        if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            renderLoadingScreen(FDN->getDisplay());
        }
    }
}

void Selection::onStateDismounted(Device *FDN) {
    bufferTimer.invalidate();
    transitionToIdleState = false;

    FDN->getDisplay()->invalidateScreen();
    LOG_W(TAG, "Selection dismounted");

    symbolManager->resetRefreshTimer();
}

bool Selection::transitionToIdle() {
    return transitionToIdleState;
}