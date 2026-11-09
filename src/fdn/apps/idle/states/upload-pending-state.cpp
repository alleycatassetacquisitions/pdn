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
    showingFailure = false;
    uploadDone = false;
    uploadFailed = false;

    LOG_I(TAG, "Mounted — %u staged match(es)",
          static_cast<unsigned>(fdnMatchSpool->getStagedCount()));

    // One attempt per cycle, whatever its outcome.
    uploadTimer->setTimer(UPLOAD_CHECK_INTERVAL_MS);
    // One request carries the batch; queuing it switches the radio to Wi-Fi.
    uploadQueued = fdnMatchSpool->uploadPendingBatch([this](bool uploaded) {
        uploadDone = true;
        uploadFailed = !uploaded;
    });

    fallbackTimer.setTimer(FALLBACK_TIMEOUT_MS);
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void UploadPendingState::onStateLoop(FDN* fdn) {
    if (showingFailure || !uploadQueued) {
        return;
    }

    const bool timedOut = !uploadDone && fallbackTimer.expired();
    if ((uploadDone && uploadFailed) || timedOut) {
        showingFailure = true;
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
    LOG_I(TAG, "Dismounted — upload %s", uploadDone ? (uploadFailed ? "failed" : "done") : "open");
    fdn->getWirelessManager()->enablePeerCommsMode();
    glyphTimer.invalidate();
    fallbackTimer.invalidate();
    failureTimer.invalidate();
    contentReady = false;
    showingFailure = false;
    uploadQueued = false;
    uploadDone = false;
    uploadFailed = false;
}

bool UploadPendingState::transitionToIdle() {
    if (!uploadQueued) return true;
    if (showingFailure) return failureTimer.expired();
    return uploadDone && !uploadFailed;
}
