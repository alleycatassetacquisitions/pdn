#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SpikeVectorEvaluate";

SpikeVectorEvaluate::SpikeVectorEvaluate(SpikeVector* game) : State(SPIKE_EVALUATE) {
    this->game = game;
}

SpikeVectorEvaluate::~SpikeVectorEvaluate() {
    game = nullptr;
}

void SpikeVectorEvaluate::onStateMounted(Device* PDN) {
    transitionToShowState = false;
    transitionToWinState = false;
    transitionToLoseState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Check if player dodged or got hit
    if (session.cursorPosition == session.gapPosition) {
        // Dodge — player was at the gap
        session.score += 100;
        LOG_I(TAG, "Dodge! Score: %d", session.score);
    } else {
        // Hit — player was not at the gap
        session.hits++;
        LOG_I(TAG, "Hit! Hits: %d/%d", session.hits, config.hitsAllowed);
    }

    // Check for loss condition
    if (session.hits > config.hitsAllowed) {
        transitionToLoseState = true;
        return;
    }

    // Advance wave
    session.currentWave++;

    // Check for win condition
    if (session.currentWave >= config.waves) {
        transitionToWinState = true;
        return;
    }

    // Continue to next wave
    transitionToShowState = true;
}

void SpikeVectorEvaluate::onStateLoop(Device* PDN) {
    // Transitions are determined in onStateMounted — nothing to do here
}

void SpikeVectorEvaluate::onStateDismounted(Device* PDN) {
    transitionToShowState = false;
    transitionToWinState = false;
    transitionToLoseState = false;
}

bool SpikeVectorEvaluate::transitionToShow() {
    return transitionToShowState;
}

bool SpikeVectorEvaluate::transitionToWin() {
    return transitionToWinState;
}

bool SpikeVectorEvaluate::transitionToLose() {
    return transitionToLoseState;
}
