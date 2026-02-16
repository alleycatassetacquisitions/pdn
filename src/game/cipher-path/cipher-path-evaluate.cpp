#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "CipherPathEvaluate";

CipherPathEvaluate::CipherPathEvaluate(CipherPath* game) : State(CIPHER_EVALUATE) {
    this->game = game;
}

CipherPathEvaluate::~CipherPathEvaluate() {
    game = nullptr;
}

void CipherPathEvaluate::onStateMounted(Device* PDN) {
    transitionToShowState = false;
    transitionToWinState = false;
    transitionToLoseState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Check if electricity reached the output terminal (round complete)
    // flowActive == false means flow stopped (either win or circuit break)
    // flowTileIndex >= pathLength means we completed the circuit
    if (!session.flowActive && session.flowTileIndex >= session.pathLength) {
        // Round complete — award score
        session.score += 100;
        session.currentRound++;

        LOG_I(TAG, "Circuit routed. Score: %d, Round: %d/%d",
              session.score, session.currentRound, config.rounds);

        // Check if all rounds completed
        if (session.currentRound >= config.rounds) {
            transitionToWinState = true;
        } else {
            // Next round
            transitionToShowState = true;
        }
    } else {
        // Circuit break — flow stopped before reaching output
        LOG_I(TAG, "Circuit break at tile %d/%d", session.flowTileIndex, session.pathLength);
        transitionToLoseState = true;
    }
}

void CipherPathEvaluate::onStateLoop(Device* PDN) {
    // Transitions are determined in onStateMounted — nothing to do here
}

void CipherPathEvaluate::onStateDismounted(Device* PDN) {
    transitionToShowState = false;
    transitionToWinState = false;
    transitionToLoseState = false;
}

bool CipherPathEvaluate::transitionToShow() {
    return transitionToShowState;
}

bool CipherPathEvaluate::transitionToWin() {
    return transitionToWinState;
}

bool CipherPathEvaluate::transitionToLose() {
    return transitionToLoseState;
}
