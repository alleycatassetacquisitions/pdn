#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "IDLE_STATE"

IdleState::IdleState(RemotePlayerManager* remotePlayerManager,
                     HackedPlayersManager* hackedPlayersManager,
                     FDNConnectWirelessManager* fdnConnectWirelessManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::IDLE)
    , remotePlayerManager(remotePlayerManager)
    , hackedPlayersManager(hackedPlayersManager)
    , fdnConnectWirelessManager(fdnConnectWirelessManager) {}

IdleState::~IdleState() {
    remotePlayerManager       = nullptr;
    hackedPlayersManager      = nullptr;
    fdnConnectWirelessManager = nullptr;
}

void IdleState::setConnectionHandler(
    std::function<void(const std::string&, const uint8_t*)> handler) {
    connectionHandler = std::move(handler);
}

void IdleState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted");
    fdn->getLightManager()->clear();
    remotePlayerManager->consumePacketReceived();
    connectionResolved = false;
    wasConnected       = false;

    fdnConnectWirelessManager->setConnectCallback(
        [this](const std::string& playerId, const uint8_t* senderMac) {
            LOG_I(TAG, "PDN connected, player: %s", playerId.c_str());
            fdnConnectWirelessManager->setPeer(senderMac);
            if (connectionHandler) connectionHandler(playerId, senderMac);
            connectionResolved = true;
        });

    uploadTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);

    fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("ALLEYCAT", centeredTextX("ALLEYCAT"), 32)
        ->render();
}

void IdleState::onStateLoop(FDN* fdn) {
    remotePlayerManager->Update();

    bool nowConnected = isConnected();
    if (nowConnected && !wasConnected) {
        connectionResolved = true;
    }
    wasConnected = nowConnected;
}

void IdleState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    (void)fdn;
    fdnConnectWirelessManager->clearCallbacks();
    uploadTimer.invalidate();
    connectionResolved = false;
}

bool IdleState::transitionToPlayerDetected() {
    return remotePlayerManager->consumePacketReceived();
}

bool IdleState::transitionToConnectionDetected() {
    return connectionResolved;
}

bool IdleState::transitionToUploadPending() {
    return uploadTimer.expired() && !hackedPlayersManager->getPendingUploads().empty();
}
