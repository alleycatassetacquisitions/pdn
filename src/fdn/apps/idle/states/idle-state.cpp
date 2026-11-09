#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "IDLE_STATE"

IdleState::IdleState(RemotePlayerManager* remotePlayerManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator, FdnMatchSpool* fdnMatchSpool)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::IDLE)
    , remotePlayerManager(remotePlayerManager)
    , fdnMatchSpool(fdnMatchSpool) {}

IdleState::~IdleState() {
    remotePlayerManager = nullptr;
    fdnMatchSpool = nullptr;
}

void IdleState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted");
    fdn->getLightManager()->clear();
    remotePlayerManager->consumePacketReceived();

    // Armed once per cycle, not per mount: idle remounts after every proximity alert and
    // cabled session, and restarting the 5 minutes each time would starve the upload.
    if (!uploadTimer.isRunning() || uploadTimer.expired()) {
        uploadTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);
    }

    fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("ALLEYCAT", centeredTextX("ALLEYCAT"), 32)
        ->render();
}

void IdleState::onStateLoop(FDN* fdn) { remotePlayerManager->Update(); }

void IdleState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    (void)fdn;
}

bool IdleState::transitionToPlayerDetected() {
    return remotePlayerManager->consumePacketReceived();
}

bool IdleState::transitionToUploadPending() {
    return uploadTimer.expired() && fdnMatchSpool->getStagedCount() > 0;
}
