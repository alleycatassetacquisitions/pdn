#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "UPLOAD_PENDING"

UploadPendingState::UploadPendingState(FdnRecordUploader* fdnRecordUploader)
    : TypedState<FDN>(IdleStateId::UPLOAD_PENDING)
    , fdnRecordUploader(fdnRecordUploader) {}

void UploadPendingState::onStateMounted(FDN* fdn) {
    contentReady = false;

    LOG_I(TAG, "Mounted");

    // Drawn first: queuing the request switches the radio to Wi-Fi and blocks while it joins.
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());

    phase = fdnRecordUploader->uploadAllRecords() ? Phase::UPLOADING : Phase::NOT_STARTED;
}

void UploadPendingState::onStateLoop(FDN* fdn) {
    if (phase != Phase::UPLOADING) return;

    if (!fdnRecordUploader->isUploading()) {
        if (!fdnRecordUploader->didLastCycleFail()) {
            phase = Phase::UPLOADED;
            return;
        }
        phase = Phase::SHOWING_FAILURE;
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
    LOG_I(TAG, "Dismounted — upload %s", phase == Phase::UPLOADED ? "done" : "not done");
    // A fast upload leaves before the loading glyphs hand the display back to text.
    fdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    glyphTimer.invalidate();
    failureTimer.invalidate();
    contentReady = false;
    phase = Phase::NOT_STARTED;
}

bool UploadPendingState::transitionToIdle() {
    if (phase == Phase::SHOWING_FAILURE) return failureTimer.expired();
    return phase == Phase::NOT_STARTED || phase == Phase::UPLOADED;
}
