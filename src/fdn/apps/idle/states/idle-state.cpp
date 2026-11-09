#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "IDLE_STATE"

IdleState::IdleState(RemotePlayerManager* remotePlayerManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator, FdnMatchSpool* fdnMatchSpool,
                     FdnProvisioningManager* fdnProvisioningManager, SimpleTimer* uploadTimer)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::IDLE)
    , remotePlayerManager(remotePlayerManager)
    , fdnMatchSpool(fdnMatchSpool)
    , fdnProvisioningManager(fdnProvisioningManager)
    , uploadTimer(uploadTimer) {}

IdleState::~IdleState() {
    remotePlayerManager = nullptr;
    fdnMatchSpool = nullptr;
    fdnProvisioningManager = nullptr;
    uploadTimer = nullptr;
}

void IdleState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted");
    fdn->getLightManager()->clear();
    remotePlayerManager->consumePacketReceived();

    // Not restarted per mount: idle remounts after every alert and cabled session.
    if (!uploadTimer->isRunning()) {
        uploadTimer->setTimer(UPLOAD_CHECK_INTERVAL_MS);
    }

    fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("ALLEYCAT", centeredTextX("ALLEYCAT"), 32)
        ->render();
}

void IdleState::onStateLoop(FDN* fdn) {
    (void)fdn;
    remotePlayerManager->Update();
    if (uploadTimer->expired() && fdnMatchSpool->getStagedCount() == 0) {
        uploadTimer->setTimer(UPLOAD_CHECK_INTERVAL_MS);
    }
}

void IdleState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    (void)fdn;
}

bool IdleState::transitionToPlayerDetected() {
    return remotePlayerManager->consumePacketReceived();
}

bool IdleState::transitionToUploadPending() {
    // A lookup left running by an unplugged PDN would fail the upload when it ends
    // and switches the radio back to ESP-NOW, so the upload waits it out (<= 8 s).
    return uploadTimer->expired() && fdnMatchSpool->getStagedCount() > 0 &&
           !fdnProvisioningManager->isLookupInFlight();
}
