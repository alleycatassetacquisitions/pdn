#include "apps/idle/states/connection-detected-state.hpp"
#include "apps/idle/idle.hpp"
#include "device/drivers/logger.hpp"
#include "utils/display-utils.hpp"

#define TAG "CONN_DETECTED"

ConnectionDetectedState::ConnectionDetectedState(HackedPlayersManager* hackedPlayersManager,
                                                 FDNConnectWirelessManager* fdnConnectWirelessManager,
                                                 RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState(remoteDeviceCoordinator, IdleStateId::CONNECTION_DETECTED) {
    this->hackedPlayersManager = hackedPlayersManager;
    this->fdnConnectWirelessManager = fdnConnectWirelessManager;
}

ConnectionDetectedState::~ConnectionDetectedState() {
    hackedPlayersManager = nullptr;
    fdnConnectWirelessManager = nullptr;
}

void ConnectionDetectedState::receivePdnConnection(const std::string& playerId) {
    pendingPlayerId = playerId;
}

void ConnectionDetectedState::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Mounted — player: %s", pendingPlayerId.c_str());
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(PDN);
}

void ConnectionDetectedState::onStateLoop(Device* PDN) {
    if (isInGlyphLoadingPhase(PDN, glyphTimer)) return;

    if (!contentReady) {
        wasHacked = hackedPlayersManager->hasPlayerHacked(pendingPlayerId);
        LOG_I(TAG, "Player %s — hacked: %s", pendingPlayerId.c_str(), wasHacked ? "yes" : "no");
        PDN->getDisplay()->invalidateScreen()->render();
        contentReady = true;
    }
}

void ConnectionDetectedState::onStateDismounted(Device* PDN) {
    LOG_I(TAG, "Dismounted");
    glyphTimer.invalidate();
    contentReady = false;
    wasHacked = false;
    pendingPlayerId.clear();
}

bool ConnectionDetectedState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
}

bool ConnectionDetectedState::transitionToAuth() {
    return contentReady && wasHacked;
}

bool ConnectionDetectedState::transitionToUnauthorized() {
    return contentReady && !wasHacked;
}
