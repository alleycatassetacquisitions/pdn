#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "IDLE_STATE"

IdleState::IdleState(RemotePlayerManager* remotePlayerManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator,
                     FdnRecordStore* fdnRecordStore)
    : FDNConnectState(remoteDeviceCoordinator, IdleStateId::IDLE)
    , remotePlayerManager(remotePlayerManager)
    , fdnRecordStore(fdnRecordStore) {}

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
    // Consumes the cycle, so this must stay idle's last edge: an expiry a cabled PDN
    // outranks is then never asked about, and waits until idle is next free.
    uploadTimer.setTimer(UPLOAD_CHECK_INTERVAL_MS);
    return fdnRecordStore->getStoredCount(RecordKind::MATCH) > 0;
}
