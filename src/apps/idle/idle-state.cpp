#include "apps/idle/states/idle-state.hpp"
#include "apps/idle/idle-resources.hpp"
#include "device/drivers/logger.hpp"
#include "apps/idle/idle.hpp"
#include "utils/display-utils.hpp"

#define TAG "IDLE_STATE"

IdleState::IdleState(RemotePlayerManager* remotePlayerManager,
                     HackedPlayersManager* hackedPlayersManager,
                     FDNConnectWirelessManager* fdnConnectWirelessManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState(remoteDeviceCoordinator, IdleStateId::IDLE) {
    this->remotePlayerManager = remotePlayerManager;
    this->hackedPlayersManager = hackedPlayersManager;
    this->fdnConnectWirelessManager = fdnConnectWirelessManager;
}

IdleState::~IdleState() {
    remotePlayerManager = nullptr;
    hackedPlayersManager = nullptr;
    fdnConnectWirelessManager = nullptr;
}

void IdleState::setConnectionHandler(std::function<void(const std::string&, const uint8_t*)> handler) {
    connectionHandler = std::move(handler);
}

void IdleState::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Mounted");

    PDN->getLightManager()->clear();
    remotePlayerManager->consumePacketReceived(); // clear any stale flag from previous state
    connectionResolved = false;
    wasConnected       = false;

    fdnConnectWirelessManager->setConnectCallback(
        [this](const std::string& playerId, const uint8_t* senderMac) {
            LOG_I(TAG, "PDN connected, player: %s", playerId.c_str());
            fdnConnectWirelessManager->setPeer(senderMac);
            if (connectionHandler) connectionHandler(playerId, senderMac);
            connectionResolved = true;
        }
    );

    uploadTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);
}

void IdleState::onStateLoop(Device* PDN) {
    remotePlayerManager->Update();

    PDN->getDisplay()->invalidateScreen()->drawImage(glassesImage)->render();

    bool nowConnected = isConnected();
    if (nowConnected && !wasConnected) {
        connectionResolved = true;
    }
    wasConnected = nowConnected;
}

void IdleState::onStateDismounted(Device* PDN) {
    LOG_I(TAG, "Dismounted");
    fdnConnectWirelessManager->clearCallbacks();
    uploadTimer.invalidate();
    connectionResolved = false;
}

bool IdleState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
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
