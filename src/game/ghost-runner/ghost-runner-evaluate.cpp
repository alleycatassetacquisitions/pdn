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
    auto& outcome = game->getOutcome();

    LOG_I(TAG, "Round %d complete: P=%d G=%d M=%d Lives=%d Score=%d",
          session.currentRound, session.perfectCount, session.goodCount,
          session.missCount, session.livesRemaining, session.score);

    // Check for immediate loss (no lives remaining)
    if (session.livesRemaining <= 0) {
        LOG_I(TAG, "LOSE: no lives remaining");
        transitionToLoseState = true;
        return;
    }

    // Round completed — advance
    session.currentRound++;

    // Check if all rounds completed
    if (session.currentRound >= config.rounds) {
        // Evaluate win conditions based on difficulty
        bool won = false;

        int totalNotes = session.perfectCount + session.goodCount + session.missCount;
        float perfectPercent = (totalNotes > 0) ?
            (static_cast<float>(session.perfectCount) / totalNotes) : 0.0f;
        float missPercent = (totalNotes > 0) ?
            (static_cast<float>(session.missCount) / totalNotes) : 0.0f;

        if (config.dualLaneChance == 0.0f) {
            // EASY mode: every hit must be GOOD or better, PERFECTs cancel MISSes
            int effectiveGood = session.goodCount + session.perfectCount;
            int netMisses = session.missCount - session.perfectCount;
            if (netMisses < 0) netMisses = 0;

            won = (netMisses == 0 && effectiveGood >= session.goodCount);
            LOG_I(TAG, "EASY eval: effectiveGood=%d netMisses=%d won=%d",
                  effectiveGood, netMisses, won);
        } else {
            // HARD mode: 60%+ PERFECT, <=10% MISS
            won = (perfectPercent >= 0.60f && missPercent <= 0.10f);
            LOG_I(TAG, "HARD eval: P=%.2f%% M=%.2f%% won=%d",
                  perfectPercent * 100, missPercent * 100, won);
        }

        if (won) {
            transitionToWinState = true;
        } else {
            transitionToLoseState = true;
        }
        return;
    }

    // Reset for next round
    session.currentPattern.clear();
    session.currentNoteIndex = 0;
    session.upPressed = false;
    session.downPressed = false;
    session.upHoldActive = false;
    session.downHoldActive = false;
    session.upHoldNoteIndex = -1;
    session.downHoldNoteIndex = -1;

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
