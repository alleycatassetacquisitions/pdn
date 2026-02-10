#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "state/state-machine-manager.hpp"
#include "game/progress-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device-types.hpp"
#include <cstdint>
#include <cstdio>

static const char* TAG = "ChallengeComplete";

ChallengeComplete::ChallengeComplete(Player* player, StateMachineManager* smManager, ProgressManager* progressManager) :
    State(CHALLENGE_COMPLETE),
    player(player),
    smManager(smManager),
    progressManager(progressManager)
{
}

ChallengeComplete::~ChallengeComplete() {
    player = nullptr;
    smManager = nullptr;
    progressManager = nullptr;
}

void ChallengeComplete::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Challenge complete mounted");
    transitionToIdleState = false;

    // Get result from SM Manager
    const MiniGameOutcome& outcome = smManager->getLastOutcome();
    GameType gameType = smManager->getLastGameType();

    LOG_I(TAG, "Game: %s, Result: %s, Score: %d",
           getGameDisplayName(gameType),
           outcome.result == MiniGameResult::WON ? "WON" : "LOST",
           outcome.score);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    if (outcome.result == MiniGameResult::WON) {
        // Unlock the Konami button reward
        KonamiButton reward = getRewardForGame(gameType);
        player->unlockKonamiButton(reward);
        LOG_I(TAG, "Unlocked Konami button: %s", getKonamiButtonName(reward));

        // Check if hard mode was beaten -- unlock color profile
        if (outcome.hardMode) {
            player->addColorProfileEligibility(gameType);
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
        PDN->getDisplay()->drawText(getGameDisplayName(gameType), 10, 35);
        char scoreStr[16];
        snprintf(scoreStr, sizeof(scoreStr), "Score: %d", outcome.score);
        PDN->getDisplay()->drawText(scoreStr, 20, 55);
    }

    PDN->getDisplay()->render();

    // Start display timer
    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void ChallengeComplete::onStateLoop(Device* PDN) {
    displayTimer.updateTime();
    if (displayTimer.expired()) {
        transitionToIdleState = true;
    }
}

void ChallengeComplete::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();
}

bool ChallengeComplete::transitionToIdle() {
    return transitionToIdleState;
}
