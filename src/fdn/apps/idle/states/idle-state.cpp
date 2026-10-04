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

    // The operator's firmware-update trigger, the same gesture as the PDN's:
    // hold the secondary button for FIRMWARE_UPDATE_HOLD_MS. DURING_LONG_PRESS
    // fires every tick the button stays held past OneButton's own long-press
    // threshold, so this just watches longPressedMillis() cross the named
    // constant; onStateLoop promotes the raw signal into the transition flag
    // per the state-machine pattern.
    cachedFdn = fdn;
    parameterizedCallbackFunction checkFirmwareUpdateHold = [](void* ctx) {
        IdleState* idle = static_cast<IdleState*>(ctx);
        if (idle->cachedFdn == nullptr) {
            return;  // dismounted; removeButtonCallbacks() clears the slot, this is the belt
        }
        if (idle->cachedFdn->getSecondaryButton()->longPressedMillis() >= FIRMWARE_UPDATE_HOLD_MS) {
            idle->secondaryHeldForFirmwareUpdate = true;
        }
    };
    fdn->getSecondaryButton()->setButtonPress(checkFirmwareUpdateHold, this, ButtonInteraction::DURING_LONG_PRESS);

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

    if (secondaryHeldForFirmwareUpdate) {
        transitionToFirmwareUpdateState = true;
    }
}

void IdleState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    fdnConnectWirelessManager->clearCallbacks();
    uploadTimer.invalidate();
    connectionResolved = false;
    fdn->getSecondaryButton()->removeButtonCallbacks();
    // Belt, not the mechanism: removeButtonCallbacks() already clears every
    // slot on both real drivers. Kept because nothing in the ButtonDriverInterface
    // contract requires that, and DURING_LONG_PRESS is the one slot no later
    // state re-registers over.
    fdn->getSecondaryButton()->setButtonPress(nullptr, nullptr, ButtonInteraction::DURING_LONG_PRESS);
    secondaryHeldForFirmwareUpdate = false;
    transitionToFirmwareUpdateState = false;
    cachedFdn = nullptr;
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

bool IdleState::transitionToFirmwareUpdate() const {
    return transitionToFirmwareUpdateState;
}
