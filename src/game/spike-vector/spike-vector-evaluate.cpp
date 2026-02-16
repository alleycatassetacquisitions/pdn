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

    // Collision detection already happened inline during Gameplay
    // This state just routes to the next outcome

    LOG_I(TAG, "Level %d complete. Hits: %d/%d, Score: %d",
          session.currentLevel + 1, session.hits, config.hitsAllowed, session.score);

    // Check for loss condition (hits > allowed)
    if (session.hits > config.hitsAllowed) {
        LOG_I(TAG, "Too many hits — routing to Lose");
        transitionToLoseState = true;
        return;
    }

    // Advance to next level
    session.currentLevel++;

    // Check for win condition (all levels completed)
    if (session.currentLevel >= config.levels) {
        LOG_I(TAG, "All levels complete — routing to Win");
        transitionToWinState = true;
        return;
    }

    // Continue to next level
    LOG_I(TAG, "Advancing to level %d", session.currentLevel + 1);
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
