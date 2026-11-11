#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "UPLOAD_PENDING"

UploadPendingState::UploadPendingState(FdnMatchSpool* fdnMatchSpool, SimpleTimer* uploadTimer)
    : TypedState<FDN>(IdleStateId::UPLOAD_PENDING)
    , fdnMatchSpool(fdnMatchSpool)
    , uploadTimer(uploadTimer) {}

UploadPendingState::~UploadPendingState() {
    fdnMatchSpool = nullptr;
    uploadTimer = nullptr;
}

void UploadPendingState::onStateMounted(FDN* fdn) {
    contentReady = false;

    LOG_I(TAG, "Mounted — %u staged match(es)",
          static_cast<unsigned>(fdnMatchSpool->getStagedCount()));

    // One attempt per cycle, whatever its outcome.
    uploadTimer->setTimer(UPLOAD_CHECK_INTERVAL_MS);
    // One request carries the batch; queuing it switches the radio to Wi-Fi.
    // Set first: a request that fails to queue completes inside uploadPendingBatch. A
    // completion after the fallback timeout or dismount is ignored.
    phase = Phase::UPLOADING;
    if (!fdnMatchSpool->uploadPendingBatch([this](bool uploaded) {
            if (phase == Phase::UPLOADING) phase = uploaded ? Phase::UPLOADED : Phase::FAILED;
        })) {
        phase = Phase::NOT_QUEUED;
    }

    fallbackTimer.setTimer(FALLBACK_TIMEOUT_MS);
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void UploadPendingState::onStateLoop(FDN* fdn) {
    if (phase == Phase::UPLOADING && fallbackTimer.expired()) phase = Phase::FAILED;
    if (phase == Phase::FAILED) {
        phase = Phase::SHOWING_FAILURE;
        fdn->getWirelessManager()->enablePeerCommsMode();
        fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT)
        ->drawText("SYNC", centeredTextX("SYNC"), 24)
        ->drawText("FAILED", centeredTextX("FAILED"), 42)
        ->render();
        failureTimer.setTimer(FAILURE_DISPLAY_MS);
        return;
    }

    if (phase != Phase::UPLOADING) return;
    if (isInGlyphLoadingPhase(fdn->getDisplay(), glyphTimer)) return;

    if (!contentReady) {
        fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("SYNCING", centeredTextX("SYNCING"), 32)
        ->render();
        contentReady = true;
    }
}

void UploadPendingState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted — upload %s", phase == Phase::UPLOADED ? "done" : "not done");
    fdn->getWirelessManager()->enablePeerCommsMode();
    glyphTimer.invalidate();
    fallbackTimer.invalidate();
    failureTimer.invalidate();
    contentReady = false;
    phase = Phase::NOT_QUEUED;
}

bool UploadPendingState::transitionToIdle() {
    if (phase == Phase::SHOWING_FAILURE) return failureTimer.expired();
    return phase == Phase::NOT_QUEUED || phase == Phase::UPLOADED;
}
