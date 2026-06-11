#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "UNAUTH_DETECTED"

UnauthorizedDetectedState::UnauthorizedDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::UNAUTHORIZED_PDN) {}

UnauthorizedDetectedState::~UnauthorizedDetectedState() {}

void UnauthorizedDetectedState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted — new player");
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void UnauthorizedDetectedState::onStateLoop(FDN* fdn) {
    if (isInGlyphLoadingPhase(fdn->getDisplay(), glyphTimer)) return;

    if (!contentReady) {
        fdn->getDisplay()
            ->invalidateScreen()
            ->drawText(ACCESS_DENIED_MESSAGE[0], centeredTextX(ACCESS_DENIED_MESSAGE[0]), 28)
            ->drawText(ACCESS_DENIED_MESSAGE[1], centeredTextX(ACCESS_DENIED_MESSAGE[1]), 44)
            ->render();
        switchTimer.setTimer(SWITCH_DELAY_MS);
        contentReady = true;
    }
}

void UnauthorizedDetectedState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    glyphTimer.invalidate();
    switchTimer.invalidate();
    contentReady = false;
}

bool UnauthorizedDetectedState::transitionToIdle() {
    return !isConnected();
}

bool UnauthorizedDetectedState::transitionToHacking() {
    return contentReady && switchTimer.expired();
}
