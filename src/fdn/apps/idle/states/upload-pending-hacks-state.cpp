#include "apps/idle/idle-states.hpp"
#include "utils/display-utils.hpp"
#include "wireless/wireless-types.hpp"
#include "fdn-constants.hpp"
#include "device/drivers/logger.hpp"

#define TAG "UPLOAD_PENDING"

UploadPendingHacksState::UploadPendingHacksState(HackedPlayersManager* hackedPlayersManager,
                                                 FdnMatchSpool* fdnMatchSpool)
    : TypedState<FDN>(IdleStateId::UPLOAD_PENDING)
    , hackedPlayersManager(hackedPlayersManager)
    , fdnMatchSpool(fdnMatchSpool) {}

UploadPendingHacksState::~UploadPendingHacksState() {
    hackedPlayersManager = nullptr;
    fdnMatchSpool = nullptr;
}

void UploadPendingHacksState::onStateMounted(FDN* fdn) {
    contentReady   = false;
    showingFailure = false;
    completedCount = 0;
    failedCount = 0;

    auto pending  = hackedPlayersManager->getPendingUploads();
    pendingCount  = static_cast<int>(pending.size());

    LOG_I(TAG, "Mounted — %d pending upload(s)", pendingCount);

    const bool matchesStaged = fdnMatchSpool->getStagedCount() > 0;
    if (pendingCount > 0 || matchesStaged) {
        fdn->getWirelessManager()->enableWifiMode();
    }

    // The whole spool goes up as one request, counted as one pending upload.
    if (matchesStaged && fdnMatchSpool->uploadPendingBatch([this](bool uploaded) {
            completedCount++;
            if (!uploaded) failedCount++;
        })) {
        pendingCount++;
    }

    for (const auto& playerId : pending) {
        std::string payload =
            "{\"playerId\":\"" + playerId +
            "\",\"boxId\":"    + std::to_string(static_cast<int>(FDN_BOX_ID)) +
            ",\"hacked\":true}";

        HttpRequest req(
            "/api/boxes",
            "POST",
            "application/json",
            payload,
            [this, playerId](const std::string&) {
                LOG_I(TAG, "Upload succeeded for %s", playerId.c_str());
                hackedPlayersManager->playerHackUploaded(playerId);
                completedCount++;
            },
            [this, playerId](const WirelessErrorInfo& err) {
                LOG_E(TAG, "Upload failed for %s: %s", playerId.c_str(), err.message.c_str());
                completedCount++;
                failedCount++;
            });

        fdn->getWirelessManager()->queueHttpRequest(std::move(req));
    }

    fallbackTimer.setTimer(FALLBACK_TIMEOUT_MS);
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void UploadPendingHacksState::onStateLoop(FDN* fdn) {
    if (showingFailure) {
        return;
    }

    const bool allDoneWithFailure =
    (pendingCount > 0 && completedCount >= pendingCount && failedCount > 0);
    const bool timedOut =
    (pendingCount > 0 && completedCount < pendingCount && fallbackTimer.expired());
    if (allDoneWithFailure || timedOut) {
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

void UploadPendingHacksState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted — %d/%d uploads completed (%d failed)", completedCount, pendingCount,
          failedCount);
    fdn->getWirelessManager()->enablePeerCommsMode();
    glyphTimer.invalidate();
    fallbackTimer.invalidate();
    failureTimer.invalidate();
    contentReady   = false;
    showingFailure = false;
    pendingCount   = 0;
    completedCount = 0;
    failedCount = 0;
}

bool UploadPendingHacksState::transitionToIdle() {
    if (pendingCount == 0) return true;
    if (showingFailure) return failureTimer.expired();
    return completedCount >= pendingCount && failedCount == 0;
}
