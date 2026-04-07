#include "apps/idle/states/unauthorized-detected-state.hpp"
#include "apps/hacking/hacking.hpp"
#include "device/drivers/logger.hpp"
#include "apps/idle/idle.hpp"
#include "utils/display-utils.hpp"

#define TAG "UNAUTH_DETECTED"

UnauthorizedDetectedState::UnauthorizedDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState(remoteDeviceCoordinator, IdleStateId::UNAUTHORIZED_PDN) {}

UnauthorizedDetectedState::~UnauthorizedDetectedState() {}

void UnauthorizedDetectedState::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Mounted — new player, switching to hacking");
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(PDN);
}

void UnauthorizedDetectedState::onStateLoop(Device* PDN) {
    if (isInGlyphLoadingPhase(PDN, glyphTimer)) return;

    if (!contentReady) {
        PDN->getDisplay()
            ->invalidateScreen()
            ->drawText(UNAUTHORIZED_MESSAGE[0], 16, 28)
            ->drawText(UNAUTHORIZED_MESSAGE[1], 16, 44)
            ->render();
        switchTimer.setTimer(SWITCH_DELAY_MS);
        contentReady = true;
    }

    if (switchTimer.expired()) {
        PDN->setActiveApp(StateId(HACKING_APP_ID));
    }
}

void UnauthorizedDetectedState::onStateDismounted(Device* PDN) {
    glyphTimer.invalidate();
    switchTimer.invalidate();
    contentReady = false;
}

bool UnauthorizedDetectedState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
}
