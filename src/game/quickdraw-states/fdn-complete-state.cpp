#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/minigame.hpp"
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

    // Look up the minigame using the game type tracked by FdnDetected
    int lastGameType = player->getLastFdnGameType();
    int appId = (lastGameType >= 0) ? getAppIdForGame(static_cast<GameType>(lastGameType)) : -1;

    MiniGame* game = nullptr;
    if (appId >= 0) {
        game = dynamic_cast<MiniGame*>(PDN->getApp(StateId(appId)));
    }
    if (!game) {
        LOG_W(TAG, "Minigame app not found for game type %d", lastGameType);
        transitionToIdleState = true;
        return;
    }

    const MiniGameOutcome& outcome = game->getOutcome();

    LOG_I(TAG, "Result: %s, Score: %d",
           outcome.result == MiniGameResult::WON ? "WON" : "LOST",
           outcome.score);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    if (outcome.result == MiniGameResult::WON) {
        // Unlock the Konami button reward
        GameType gameType = game->getGameType();
        KonamiButton reward = getRewardForGame(gameType);
        player->unlockKonamiButton(static_cast<uint8_t>(reward));
        LOG_I(TAG, "Unlocked Konami button: %s", getKonamiButtonName(reward));

        // Check if hard mode was beaten — unlock color profile
        if (outcome.hardMode) {
            player->addColorProfileEligibility(static_cast<int>(gameType));
            player->setPendingProfileGame(static_cast<int>(gameType));
            LOG_I(TAG, "Unlocked color profile for: %s", getGameDisplayName(gameType));
        }

        // Auto-boon: when all 7 Konami buttons are collected
        if (player->isKonamiComplete() && !player->hasKonamiBoon()) {
            player->setKonamiBoon(true);
            LOG_I(TAG, "All 7 Konami buttons collected — BOON granted!");
            PDN->getDisplay()->drawText("KONAMI COMPLETE!", 5, 45);
        }

        // Save progress
        if (progressManager) {
            progressManager->saveProgress();
            // Attempt server sync
            progressManager->syncProgress(PDN->getHttpClient());
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
