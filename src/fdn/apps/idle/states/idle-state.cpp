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

    // Not restarted per mount: idle remounts after every alert and cabled session.
    if (!uploadTimer.isRunning()) {
        uploadTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);
    }

    fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("ALLEYCAT", centeredTextX("ALLEYCAT"), 32)
        ->render();
}

void IdleState::onStateLoop(FDN* fdn) {
    (void)fdn;
    remotePlayerManager->Update();
    if (uploadTimer.expired() && fdnMatchSpool->getStagedCount() == 0) {
        uploadTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);
    }
}

void IdleState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    (void)fdn;
    // One upload attempt per cycle, whatever its outcome.
    if (uploadTimer.expired()) {
        uploadTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);
    }
}

bool IdleState::transitionToPlayerDetected() {
    return remotePlayerManager->consumePacketReceived();
}

bool IdleState::transitionToUploadPending() {
    return uploadTimer.expired() && fdnMatchSpool->getStagedCount() > 0;
}
