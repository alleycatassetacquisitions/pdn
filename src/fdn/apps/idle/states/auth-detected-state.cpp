#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "AUTH_DETECTED"

AuthDetectedState::AuthDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                     FDNConnectWirelessManager* fdnConnectWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::AUTHORIZED_PDN)
    , fdnConnectWirelessManager(fdnConnectWirelessManager) {}

AuthDetectedState::~AuthDetectedState() {
    fdnConnectWirelessManager = nullptr;
}

void AuthDetectedState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted — authorized player");
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void AuthDetectedState::onStateLoop(FDN* fdn) {
    if (isInGlyphLoadingPhase(fdn->getDisplay(), glyphTimer)) return;

    if (!contentReady) {
        const std::string& peerId = fdnConnectWirelessManager->getPeerPlayerId();
        fdn->getDisplay()
            ->invalidateScreen()
            ->drawText(AUTH_MESSAGE[0], centeredTextX(AUTH_MESSAGE[0]), 20)
            ->drawText(AUTH_MESSAGE[1], centeredTextX(AUTH_MESSAGE[1]), 36)
            ->drawText(peerId.c_str(), centeredTextX(peerId.c_str()), 52)
            ->render();
        switchTimer.setTimer(SWITCH_DELAY_MS);
        contentReady = true;
    }
}

void AuthDetectedState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    glyphTimer.invalidate();
    switchTimer.invalidate();
    contentReady = false;
}

bool AuthDetectedState::transitionToIdle() {
    return !isConnected();
}

bool AuthDetectedState::transitionToMainMenu() {
    return contentReady && switchTimer.expired();
}
