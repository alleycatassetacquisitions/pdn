#include "apps/symbol-match/symbol-match-states.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/wireless-types.hpp"
#include "fdn-constants.hpp"
#include <string>

#define TAG "SYMBOL_UPLOAD_PENDING"

SymbolMatchUploadPendingHacksState::SymbolMatchUploadPendingHacksState(HackedPlayersManager* hackedPlayersManager)
    : TypedState<FDN>(SymbolMatchStateId::SYMBOL_MATCH_UPLOAD_PENDING)
    , hackedPlayersManager(hackedPlayersManager) {}

SymbolMatchUploadPendingHacksState::~SymbolMatchUploadPendingHacksState() {
    hackedPlayersManager = nullptr;
}

void SymbolMatchUploadPendingHacksState::onStateMounted(FDN* fdn) {
    contentReady  = false;
    completedCount = 0;

    auto pending = hackedPlayersManager->getPendingUploads();
    pendingCount  = static_cast<int>(pending.size());
    LOG_I(TAG, "Mounted - %d pending upload(s)", pendingCount);

    for (const auto& playerId : pending) {
        std::string payload =
            "{\"playerId\":\"" + playerId +
            "\",\"boxId\":" + std::to_string(static_cast<int>(FDN_BOX_ID)) +
            ",\"hacked\":true}";

        HttpRequest req(
            "/api/boxes",
            "POST",
            payload,
            [this, playerId](const std::string&) {
                LOG_I(TAG, "Upload succeeded for %s", playerId.c_str());
                hackedPlayersManager->playerHackUploaded(playerId);
                completedCount++;
            },
            [this, playerId](const WirelessErrorInfo& err) {
                LOG_E(TAG, "Upload failed for %s: %s", playerId.c_str(), err.message.c_str());
                completedCount++;
            });

        fdn->getHttpClient()->queueRequest(req);
    }

    fallbackTimer.setTimer(FALLBACK_TIMEOUT_MS);
    glyphTimer.setTimer(GLYPH_LOADING_DURATION_MS);
    showLoadingGlyphs(fdn->getDisplay());
}

void SymbolMatchUploadPendingHacksState::onStateLoop(FDN* fdn) {
    if (isInGlyphLoadingPhase(fdn->getDisplay(), glyphTimer)) return;

    if (!contentReady) {
        fdn->getDisplay()
            ->invalidateScreen()
            ->drawText("SYNCING", 30, 32)
            ->render();
        contentReady = true;
    }
}

void SymbolMatchUploadPendingHacksState::onStateDismounted(FDN* fdn) {
    (void)fdn;
    LOG_I(TAG, "Dismounted - %d/%d uploads completed", completedCount, pendingCount);
    glyphTimer.invalidate();
    fallbackTimer.invalidate();
    contentReady   = false;
    pendingCount   = 0;
    completedCount = 0;
}

bool SymbolMatchUploadPendingHacksState::transitionToIdle() {
    if (pendingCount == 0) return true;
    return completedCount >= pendingCount || fallbackTimer.expired();
}
