#include "apps/symbol-match/symbol-match-states.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "SYMBOL_AUTH_DETECTED"

SymbolMatchAuthDetectedState::SymbolMatchAuthDetectedState(
    RemoteDeviceCoordinator* remoteDeviceCoordinator,
    FDNConnectWirelessManager* fdnConnectWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, SymbolMatchStateId::SYMBOL_MATCH_AUTHORIZED_PDN)
    , fdnConnectWirelessManager(fdnConnectWirelessManager) {}

SymbolMatchAuthDetectedState::~SymbolMatchAuthDetectedState() {
    fdnConnectWirelessManager = nullptr;
}

void SymbolMatchAuthDetectedState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted - authorized player");
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void SymbolMatchAuthDetectedState::onStateLoop(FDN* fdn) {
    if (isInGlyphLoadingPhase(fdn->getDisplay(), glyphTimer)) return;

    if (!contentReady) {
        const std::string& pid = fdnConnectWirelessManager->getPeerPlayerId();
        fdn->getDisplay()
            ->invalidateScreen()
            ->drawText(authMessage[0], centeredTextX(authMessage[0]), 20)
            ->drawText(authMessage[1], centeredTextX(authMessage[1]), 36)
            ->drawText(pid.c_str(), centeredTextX(pid.c_str()), 52)
            ->render();
        switchTimer.setTimer(SWITCH_DELAY_MS);
        contentReady = true;
    }
}

void SymbolMatchAuthDetectedState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    glyphTimer.invalidate();
    switchTimer.invalidate();
    contentReady = false;
}

bool SymbolMatchAuthDetectedState::transitionToSelection() {
    return !isConnected();
}

bool SymbolMatchAuthDetectedState::transitionToMainMenu() {
    return contentReady && switchTimer.expired();
}
