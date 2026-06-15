#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "CONN_DETECTED"

ConnectionDetectedState::ConnectionDetectedState(HackedPlayersManager* hackedPlayersManager,
                                                 FDNConnectWirelessManager* fdnConnectWirelessManager,
                                                 RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::CONNECTION_DETECTED)
    , hackedPlayersManager(hackedPlayersManager)
    , fdnConnectWirelessManager(fdnConnectWirelessManager) {}

ConnectionDetectedState::~ConnectionDetectedState() {
    hackedPlayersManager      = nullptr;
    fdnConnectWirelessManager = nullptr;
}

void ConnectionDetectedState::receivePdnConnection(const std::string& playerId) {
    pendingPlayerId = playerId;
}

void ConnectionDetectedState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted — player: %s", pendingPlayerId.c_str());
    contentReady = false;
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void ConnectionDetectedState::onStateLoop(FDN* fdn) {
    if (isInGlyphLoadingPhase(fdn->getDisplay(), glyphTimer)) return;

    if (!contentReady) {
        wasHacked = hackedPlayersManager->hasPlayerHacked(pendingPlayerId);
        LOG_I(TAG, "Player %s — hacked: %s", pendingPlayerId.c_str(), wasHacked ? "yes" : "no");
        fdn->getDisplay()->invalidateScreen()->render();
        contentReady = true;
    }
}

void ConnectionDetectedState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    (void)fdn;
    glyphTimer.invalidate();
    contentReady    = false;
    wasHacked       = false;
    pendingPlayerId.clear();
}

bool ConnectionDetectedState::transitionToAuth() {
    return contentReady && wasHacked;
}

bool ConnectionDetectedState::transitionToUnauthorized() {
    return contentReady && !wasHacked;
}

bool ConnectionDetectedState::transitionToIdle() {
    return !isConnected();
}
