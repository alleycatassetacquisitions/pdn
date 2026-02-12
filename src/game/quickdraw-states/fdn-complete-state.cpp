#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/progress-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device-types.hpp"
#include <cstdint>
#include <cstdio>

static const char* TAG = "FdnComplete";

FdnComplete::FdnComplete(Player* player, ProgressManager* progressManager) :
    State(FDN_COMPLETE),
    player(player),
    progressManager(progressManager)
{
}

FdnComplete::~FdnComplete() {
    player = nullptr;
    progressManager = nullptr;
}

void FdnComplete::onStateMounted(Device* PDN) {
    LOG_I(TAG, "FDN complete mounted");
    transitionToIdleState = false;

    // Read the outcome from Signal Echo
    auto* echo = dynamic_cast<MiniGame*>(PDN->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    if (!echo) {
        LOG_W(TAG, "Signal Echo not found in AppConfig");
        transitionToIdleState = true;
        return;
    }

    const MiniGameOutcome& outcome = echo->getOutcome();

    LOG_I(TAG, "Result: %s, Score: %d",
           outcome.result == MiniGameResult::WON ? "WON" : "LOST",
           outcome.score);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    if (outcome.result == MiniGameResult::WON) {
        // Unlock the Konami button reward
        GameType gameType = echo->getGameType();
        KonamiButton reward = getRewardForGame(gameType);
        player->unlockKonamiButton(static_cast<uint8_t>(reward));
        LOG_I(TAG, "Unlocked Konami button: %s", getKonamiButtonName(reward));

        // Check if hard mode was beaten — unlock color profile
        if (outcome.hardMode) {
            player->addColorProfileEligibility(static_cast<int>(gameType));
            LOG_I(TAG, "Unlocked color profile for: %s", getGameDisplayName(gameType));
        }

        // Save progress
        if (progressManager) {
            progressManager->saveProgress();
        }

        // Display victory
        PDN->getDisplay()->drawText("VICTORY!", 20, 15);
        PDN->getDisplay()->drawText(getGameDisplayName(gameType), 10, 35);
        char scoreStr[16];
        snprintf(scoreStr, sizeof(scoreStr), "Score: %d", outcome.score);
        PDN->getDisplay()->drawText(scoreStr, 20, 55);
    } else {
        // Display loss
        PDN->getDisplay()->drawText("DEFEATED", 20, 15);
        char scoreStr[16];
        snprintf(scoreStr, sizeof(scoreStr), "Score: %d", outcome.score);
        PDN->getDisplay()->drawText(scoreStr, 20, 35);
    }

    PDN->getDisplay()->render();

    // Start display timer
    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void FdnComplete::onStateLoop(Device* PDN) {
    displayTimer.updateTime();
    if (displayTimer.expired()) {
        transitionToIdleState = true;
    }
}

void FdnComplete::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();
    player->clearPendingChallenge();
}

bool FdnComplete::transitionToIdle() {
    return transitionToIdleState;
}
