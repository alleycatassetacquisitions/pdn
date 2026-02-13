#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "GhostRunnerEvaluate";

GhostRunnerEvaluate::GhostRunnerEvaluate(GhostRunner* game) : State(GHOST_EVALUATE) {
    this->game = game;
}

GhostRunnerEvaluate::~GhostRunnerEvaluate() {
    game = nullptr;
}

void GhostRunnerEvaluate::onStateMounted(Device* PDN) {
    transitionToShowState = false;
    transitionToWinState = false;
    transitionToLoseState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Determine if the press was a hit or miss
    bool hit = false;
    if (session.playerPressed) {
        // Player pressed — check if ghost was in target zone
        if (session.ghostPosition >= config.targetZoneStart &&
            session.ghostPosition <= config.targetZoneEnd) {
            hit = true;
            session.score += 100;
            LOG_I(TAG, "HIT at position %d (zone %d-%d), score=%d",
                  session.ghostPosition, config.targetZoneStart,
                  config.targetZoneEnd, session.score);
        } else {
            // Press outside target zone = strike
            session.strikes++;
            LOG_I(TAG, "MISS at position %d (zone %d-%d), strikes=%d",
                  session.ghostPosition, config.targetZoneStart,
                  config.targetZoneEnd, session.strikes);
        }
    } else {
        // Ghost reached end without press = strike (timeout)
        session.strikes++;
        LOG_I(TAG, "TIMEOUT — ghost reached end, strikes=%d", session.strikes);
    }

    // Check if player has exceeded allowed strikes
    if (session.strikes > config.missesAllowed) {
        transitionToLoseState = true;
        return;
    }

    // Round completed — advance
    session.currentRound++;

    // Check if all rounds completed
    if (session.currentRound >= config.rounds) {
        transitionToWinState = true;
        return;
    }

    // Reset for next round
    session.ghostPosition = 0;
    session.playerPressed = false;

    transitionToShowState = true;
}

void GhostRunnerEvaluate::onStateLoop(Device* PDN) {
    // Transitions are determined in onStateMounted — nothing to do here
}

void GhostRunnerEvaluate::onStateDismounted(Device* PDN) {
    transitionToShowState = false;
    transitionToWinState = false;
    transitionToLoseState = false;
}

bool GhostRunnerEvaluate::transitionToShow() {
    return transitionToShowState;
}

bool GhostRunnerEvaluate::transitionToWin() {
    return transitionToWinState;
}

bool GhostRunnerEvaluate::transitionToLose() {
    return transitionToLoseState;
}
