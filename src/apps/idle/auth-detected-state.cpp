#include "apps/idle/states/auth-detected-state.hpp"
#include "device/drivers/logger.hpp"
#include "apps/idle/idle.hpp"
#include "utils/display-utils.hpp"

#define TAG "AUTH_DETECTED"

AuthDetectedState::AuthDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator, FDNConnectWirelessManager* fdnConnectWirelessManager)
    : ConnectState(remoteDeviceCoordinator, IdleStateId::AUTHORIZED_PDN) {
    this->fdnConnectWirelessManager = fdnConnectWirelessManager;
}

AuthDetectedState::~AuthDetectedState() {
    fdnConnectWirelessManager = nullptr;
}

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
            ->drawText(AUTH_MESSAGE[0], centeredTextX(AUTH_MESSAGE[0]), 20)
            ->drawText(AUTH_MESSAGE[1], centeredTextX(AUTH_MESSAGE[1]), 36)
            ->drawText(fdnConnectWirelessManager->getPeerPlayerId().c_str(), centeredTextX(fdnConnectWirelessManager->getPeerPlayerId().c_str()), 52)
            ->render();
        switchTimer.setTimer(SWITCH_DELAY_MS);
        contentReady = true;
    }

    // App-level transition to main menu is handled via AppTransition in idle.cpp
}

void AuthDetectedState::onStateDismounted(Device* PDN) {
    glyphTimer.invalidate();
    switchTimer.invalidate();
    contentReady = false;
}

bool AuthDetectedState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
}

bool AuthDetectedState::transitionToIdle() {
    return !isConnected();
}

bool AuthDetectedState::transitionToMainMenu() {
    return contentReady && switchTimer.expired();
}
