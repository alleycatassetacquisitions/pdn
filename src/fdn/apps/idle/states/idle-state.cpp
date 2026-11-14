#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "IDLE_STATE"

IdleState::IdleState(RemotePlayerManager* remotePlayerManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator,
                     FdnRecordUploader* fdnRecordUploader)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::IDLE)
    , remotePlayerManager(remotePlayerManager)
    , fdnRecordUploader(fdnRecordUploader) {}

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

void IdleState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    (void)fdn;
}

bool IdleState::transitionToPlayerDetected() {
    return remotePlayerManager->consumePacketReceived();
}

bool IdleState::transitionToUploadPending() {
    if (!uploadTimer.expired()) return false;
    // Consumes the expiry, so this must stay idle's last edge.
    uploadTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);
    return fdnRecordUploader->hasRecordsWaiting();
}
