#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "UPLOAD_PENDING"

UploadPendingState::UploadPendingState(FdnRecordStore* fdnRecordStore)
    : TypedState<FDN>(IdleStateId::UPLOAD_PENDING)
    , fdnRecordStore(fdnRecordStore) {}

void UploadPendingState::onStateMounted(FDN* fdn) {
    contentReady = false;

    LOG_I(TAG, "Mounted — %u stored match record(s)",
          static_cast<unsigned>(fdnRecordStore->getStoredCount(RecordKind::MATCH)));

    // Drawn first: queuing the request switches the radio to Wi-Fi and blocks while it joins.
    fallbackTimer.setTimer(FALLBACK_TIMEOUT_MS);
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());

    // UPLOADING is set first because a request that fails to queue completes inside
    // uploadPendingBatch; a completion after the fallback timeout or dismount is ignored.
    phase = Phase::UPLOADING;
    if (!fdnRecordStore->uploadPendingBatch([this](bool uploaded) {
            if (phase == Phase::UPLOADING) phase = uploaded ? Phase::UPLOADED : Phase::FAILED;
        })) {
        phase = Phase::NOT_STARTED;
    }
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
    // The failure screen already handed the radio back. A second switch would cut
    // off a PIN lookup that a PDN cabled during that screen has since started.
    if (phase == Phase::UPLOADING || phase == Phase::UPLOADED) {
        fdn->getWirelessManager()->enablePeerCommsMode();
    }
    // A fast upload leaves before the loading glyphs hand the display back to text.
    fdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    glyphTimer.invalidate();
    fallbackTimer.invalidate();
    failureTimer.invalidate();
    contentReady = false;
    phase = Phase::NOT_STARTED;
}

bool UploadPendingState::transitionToIdle() {
    if (phase == Phase::SHOWING_FAILURE) return failureTimer.expired();
    return phase == Phase::NOT_STARTED || phase == Phase::UPLOADED;
}
