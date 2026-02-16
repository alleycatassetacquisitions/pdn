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

    LOG_I(TAG, "Round %d evaluation: Steps=%d Bonks=%d Lives=%d",
          session.currentRound, session.stepsUsed, session.bonkCount,
          session.livesRemaining);

    // Calculate score for this round
    // 100 per correct move (optimal path length)
    int optimalMoves = session.solutionLength;
    int roundScore = optimalMoves * 100;

    // Bonus for zero bonks (perfect memory)
    if (session.bonkCount == 0) {
        roundScore += 500;
        LOG_I(TAG, "Zero-bonk bonus: +500");
    }

    session.score += roundScore;
    LOG_I(TAG, "Round score: %d, Total score: %d", roundScore, session.score);

    // Check for immediate loss (no lives remaining)
    if (session.livesRemaining <= 0) {
        LOG_I(TAG, "LOSE: no lives remaining");
        transitionToLoseState = true;
        return;
    }

    // Advance to next round
    session.currentRound++;

    // Check if all rounds completed
    if (session.currentRound >= config.rounds) {
        LOG_I(TAG, "All rounds complete - WIN! Final score: %d", session.score);
        transitionToWinState = true;
        return;
    }

    // Continue to next round
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
