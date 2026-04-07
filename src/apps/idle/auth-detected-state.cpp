#include "apps/idle/states/auth-detected-state.hpp"
#include "apps/main-menu/main-menu.hpp"
#include "device/drivers/logger.hpp"
#include "apps/idle/idle.hpp"
#include "utils/display-utils.hpp"

#define TAG "AUTH_DETECTED"

AuthDetectedState::AuthDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState(remoteDeviceCoordinator, IdleStateId::AUTHORIZED_PDN) {}

AuthDetectedState::~AuthDetectedState() {}

void AuthDetectedState::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Mounted — authorized player, switching to main menu");
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(PDN);
}

void AuthDetectedState::onStateLoop(Device* PDN) {
    if (isInGlyphLoadingPhase(PDN, glyphTimer)) {
        return;
    }

    if (!contentReady) {
        PDN->getDisplay()
            ->invalidateScreen()
            ->drawText(AUTH_MESSAGE[0], 16, 28)
            ->drawText(AUTH_MESSAGE[1], 16, 44)
            ->render();
        switchTimer.setTimer(SWITCH_DELAY_MS);
        contentReady = true;
    }

    if (switchTimer.expired()) {
        PDN->setActiveApp(StateId(MAIN_MENU_APP_ID));
    }
}

void AuthDetectedState::onStateDismounted(Device* PDN) {
    glyphTimer.invalidate();
    switchTimer.invalidate();
    contentReady = false;
}

bool AuthDetectedState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
}
