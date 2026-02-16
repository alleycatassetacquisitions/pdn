#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "DecryptEvaluate";

DecryptEvaluate::DecryptEvaluate(FirewallDecrypt* game) : State(DECRYPT_EVALUATE) {
    this->game = game;
}

DecryptEvaluate::~DecryptEvaluate() {
    game = nullptr;
}

void DecryptEvaluate::onStateMounted(Device* PDN) {
    transitionToScanState = false;
    transitionToWinState = false;
    transitionToLoseState = false;

    auto& session = game->getSession();

    if (session.timedOut) {
        LOG_I(TAG, "Timed out — lose");
        transitionToLoseState = true;
        return;
    }

    // Check if selection matches target (target is always at index 0)
    bool correct = (session.selectedIndex == 0);

    if (!correct) {
        LOG_I(TAG, "Wrong selection (picked %d, target at 0)", session.selectedIndex);

        // Decrement mistakes
        session.mistakesRemaining--;

        if (session.mistakesRemaining <= 0) {
            LOG_I(TAG, "No mistakes remaining — lose");
            transitionToLoseState = true;
        } else {
            LOG_I(TAG, "Mistakes remaining: %d — retry", session.mistakesRemaining);

            // Haptic feedback for wrong answer
            PDN->getHaptics()->setIntensity(200);

            // Go back to scan for retry
            transitionToScanState = true;
        }
        return;
    }

    // Correct match
    session.score += 100;
    session.currentRound++;
    LOG_I(TAG, "Correct! Round %d/%d, Score: %d",
          session.currentRound, game->getConfig().numRounds, session.score);

    // Brief haptic success feedback
    PDN->getHaptics()->setIntensity(150);

    if (session.currentRound >= game->getConfig().numRounds) {
        // All rounds completed — win
        transitionToWinState = true;
    } else {
        // Set up next round
        game->setupRound();
        transitionToScanState = true;
    }
}

void DecryptEvaluate::onStateLoop(Device* PDN) {
    // Transitions are set in onStateMounted
}

void DecryptEvaluate::onStateDismounted(Device* PDN) {
    PDN->getHaptics()->off();
}

bool DecryptEvaluate::transitionToScan() {
    return transitionToScanState;
}

bool DecryptEvaluate::transitionToWin() {
    return transitionToWinState;
}

bool DecryptEvaluate::transitionToLose() {
    return transitionToLoseState;
}
