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

    // Check if selection matches target
    bool correct = (session.selectedIndex == session.targetIndex);

    if (!correct) {
        LOG_I(TAG, "Wrong selection (picked %d, target at %d) — lose",
              session.selectedIndex, session.targetIndex);
        transitionToLoseState = true;
        return;
    }

    // Correct match
    session.score += 100;
    session.currentRound++;
    LOG_I(TAG, "Correct! Round %d/%d, Score: %d",
          session.currentRound, game->getConfig().numRounds, session.score);

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
    // Nothing to clean up
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
