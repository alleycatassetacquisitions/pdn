#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "device/drivers/logger.hpp"
#include <cstdio>

static const char* TAG = "BreachDefenseEvaluate";

BreachDefenseEvaluate::BreachDefenseEvaluate(BreachDefense* game) : State(BREACH_EVALUATE) {
    this->game = game;
}

BreachDefenseEvaluate::~BreachDefenseEvaluate() {
    game = nullptr;
}

void BreachDefenseEvaluate::onStateMounted(Device* PDN) {
    transitionToShowState = false;
    transitionToWinState = false;
    transitionToLoseState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    bool blocked = (session.shieldLane == session.threatLane);

    if (blocked) {
        session.score += 100;
        LOG_I(TAG, "BLOCKED! Shield %d == Threat %d. Score: %d",
              session.shieldLane, session.threatLane, session.score);

        PDN->getDisplay()->invalidateScreen();
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("BLOCKED!", 30, 30);
        PDN->getDisplay()->render();

        PDN->getHaptics()->setIntensity(150);
    } else {
        session.breaches++;
        LOG_I(TAG, "BREACH! Shield %d != Threat %d. Breaches: %d/%d",
              session.shieldLane, session.threatLane,
              session.breaches, config.missesAllowed);

        PDN->getDisplay()->invalidateScreen();
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("BREACH!", 35, 30);
        PDN->getDisplay()->render();

        PDN->getHaptics()->setIntensity(255);
    }

    // Advance threat counter
    session.currentThreat++;

    // Determine next state after eval timer
    evalTimer.setTimer(EVAL_DURATION_MS);
}

void BreachDefenseEvaluate::onStateLoop(Device* PDN) {
    if (evalTimer.expired()) {
        PDN->getHaptics()->off();

        auto& session = game->getSession();
        auto& config = game->getConfig();

        // Check lose condition first (breaches exceed allowed)
        if (session.breaches > config.missesAllowed) {
            transitionToLoseState = true;
        }
        // Check win condition (all threats survived)
        else if (session.currentThreat >= config.totalThreats) {
            transitionToWinState = true;
        }
        // More threats remain
        else {
            transitionToShowState = true;
        }
    }
}

void BreachDefenseEvaluate::onStateDismounted(Device* PDN) {
    evalTimer.invalidate();
    transitionToShowState = false;
    transitionToWinState = false;
    transitionToLoseState = false;
    PDN->getHaptics()->off();
}

bool BreachDefenseEvaluate::transitionToShow() {
    return transitionToShowState;
}

bool BreachDefenseEvaluate::transitionToWin() {
    return transitionToWinState;
}

bool BreachDefenseEvaluate::transitionToLose() {
    return transitionToLoseState;
}
