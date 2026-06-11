#include "apps/symbol-match/symbol-match-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "SYMBOL_UNAUTH_DETECTED"

SymbolMatchUnauthorizedDetectedState::SymbolMatchUnauthorizedDetectedState(
    RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, SymbolMatchStateId::SYMBOL_MATCH_UNAUTHORIZED_PDN) {}

SymbolMatchUnauthorizedDetectedState::~SymbolMatchUnauthorizedDetectedState() {}

void SymbolMatchUnauthorizedDetectedState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted - new player");
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void SymbolMatchUnauthorizedDetectedState::onStateLoop(FDN* fdn) {
    if (isInGlyphLoadingPhase(fdn->getDisplay(), glyphTimer)) return;

    if (!contentReady) {
        fdn->getDisplay()
            ->invalidateScreen()
            ->drawText(accessDeniedMessage[0], centeredTextX(accessDeniedMessage[0]), 28)
            ->drawText(accessDeniedMessage[1], centeredTextX(accessDeniedMessage[1]), 44)
            ->render();
        switchTimer.setTimer(SWITCH_DELAY_MS);
        contentReady = true;
    }
}

void SymbolMatchUnauthorizedDetectedState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    glyphTimer.invalidate();
    switchTimer.invalidate();
    contentReady = false;
}

bool SymbolMatchUnauthorizedDetectedState::transitionToSelection() {
    return !isConnected();
}

bool SymbolMatchUnauthorizedDetectedState::transitionToHacking() {
    return contentReady && switchTimer.expired();
}
