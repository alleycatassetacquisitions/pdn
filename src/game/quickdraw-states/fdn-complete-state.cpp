#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/minigame.hpp"
#include "game/progress-manager.hpp"
#include "game/difficulty-scaler.hpp"
#include "game/ui-clean-minimal.hpp"
#include "device/drivers/logger.hpp"
#include "device/device-types.hpp"
#include <cstdint>
#include <cstdio>

static const char* TAG = "FdnComplete";

FdnComplete::FdnComplete(Player* player, ProgressManager* progressManager, FdnResultManager* fdnResultManager, DifficultyScaler* scaler) :
    State(FDN_COMPLETE),
    player(player),
    progressManager(progressManager),
    fdnResultManager(fdnResultManager),
    scaler(scaler)
{
}

FdnComplete::~FdnComplete() {
    player = nullptr;
    progressManager = nullptr;
    fdnResultManager = nullptr;
    scaler = nullptr;
}

void FdnComplete::onStateMounted(Device* PDN) {
    LOG_I(TAG, "FDN complete mounted");
    transitionToIdleState = false;
    transitionToColorPromptState = false;

    // Look up the minigame using the game type tracked by FdnDetected
    int lastGameType = player->getLastFdnGameType();
    int appId = (lastGameType >= 0) ? getAppIdForGame(static_cast<GameType>(lastGameType)) : -1;

    MiniGame* game = nullptr;
    if (appId >= 0) {
        game = static_cast<MiniGame*>(PDN->getApp(StateId(appId)));
    }
    if (!game) {
        LOG_W(TAG, "Minigame app not found for game type %d", lastGameType);
        transitionToIdleState = true;
        return;
    }

    const MiniGameOutcome& outcome = game->getOutcome();
    GameType gameType = static_cast<GameType>(lastGameType);
    bool won = (outcome.result == MiniGameResult::WON);

    LOG_I(TAG, "Result: %s, Score: %d",
           won ? "WON" : "LOST",
           outcome.score);

    // Calculate actual completion time
    uint32_t currentTime = SimpleTimer::getPlatformClock()->milliseconds();
    uint32_t elapsedMs = (currentTime >= outcome.startTimeMs) ?
                         (currentTime - outcome.startTimeMs) : 0;

    // Record result for difficulty scaling using actual completion time
    if (scaler) {
        scaler->recordResult(gameType, won, elapsedMs);
        float newScale = scaler->getScaledDifficulty(gameType);
        std::string label = scaler->getDifficultyLabel(gameType);
        LOG_I(TAG, "Difficulty scale updated: %.2f (%s) | Time: %ums", newScale, label.c_str(), elapsedMs);
    }

    // Cache the result for upload
    if (fdnResultManager) {
        fdnResultManager->cacheResult(gameType, won, outcome.score, outcome.hardMode);
        LOG_I(TAG, "Cached FDN result: game=%d won=%d score=%d hardMode=%d",
              lastGameType, won, outcome.score, outcome.hardMode);
    }

    bool recreational = player->isRecreationalMode();

    // Record game statistics

    if (outcome.result == MiniGameResult::WON) {
        player->getGameStatsTracker().recordWin(gameType, outcome.hardMode, elapsedMs);
        LOG_I(TAG, "Recorded win: time=%ums hardMode=%d", elapsedMs, outcome.hardMode);
    } else {
        player->getGameStatsTracker().recordLoss(gameType, outcome.hardMode, elapsedMs);
        LOG_I(TAG, "Recorded loss: time=%ums hardMode=%d", elapsedMs, outcome.hardMode);
    }

    if (outcome.result == MiniGameResult::WON) {
        if (!recreational) {
            // Unlock the Konami button reward
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
            }

            // Save progress
            if (progressManager) {
                progressManager->saveProgress();
                progressManager->syncProgress(PDN->getHttpClient());
            }
        } else {
            LOG_I(TAG, "Recreational win -- no rewards");
        }

        // Display victory using clean minimal UI
        uint8_t attempts = outcome.hardMode ?
            player->getHardAttempts(gameType) :
            player->getEasyAttempts(gameType);
        UICleanMinimal::drawWinScreen(PDN->getDisplay(), outcome.score, attempts);
    } else {
        // Display loss using clean minimal UI
        UICleanMinimal::drawLoseScreen(PDN->getDisplay(), outcome.score, 0);
    }

    // Start display timer
    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void FdnComplete::onStateLoop(Device* PDN) {
    displayTimer.updateTime();
    if (displayTimer.expired()) {
        // Route to color prompt if hard mode was won and profile is pending
        if (!player->isRecreationalMode() && player->getPendingProfileGame() >= 0) {
            transitionToColorPromptState = true;
        } else {
            transitionToIdleState = true;
        }
    }
}

void FdnComplete::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();
    player->clearPendingChallenge();
    player->setRecreationalMode(false);
}

bool FdnComplete::transitionToColorPrompt() {
    return transitionToColorPromptState;
}

bool FdnComplete::transitionToIdle() {
    return transitionToIdleState;
}
