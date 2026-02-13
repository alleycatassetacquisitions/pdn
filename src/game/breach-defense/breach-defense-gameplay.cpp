#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "device/drivers/logger.hpp"
#include <cstdio>

static const char* TAG = "BreachDefenseGameplay";

BreachDefenseGameplay::BreachDefenseGameplay(BreachDefense* game) : State(BREACH_GAMEPLAY) {
    this->game = game;
}

BreachDefenseGameplay::~BreachDefenseGameplay() {
    game = nullptr;
}

void BreachDefenseGameplay::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;

    LOG_I(TAG, "Gameplay started — shield lane %d, threat lane %d",
          game->getSession().shieldLane, game->getSession().threatLane);

    // Set up button callbacks for shield movement
    parameterizedCallbackFunction upCb = [](void* ctx) {
        auto* state = static_cast<BreachDefenseGameplay*>(ctx);
        auto& sess = state->game->getSession();
        if (sess.shieldLane > 0) sess.shieldLane--;
    };
    PDN->getPrimaryButton()->setButtonPress(upCb, this, ButtonInteraction::CLICK);

    parameterizedCallbackFunction downCb = [](void* ctx) {
        auto* state = static_cast<BreachDefenseGameplay*>(ctx);
        auto& sess = state->game->getSession();
        if (sess.shieldLane < state->game->getConfig().numLanes - 1) sess.shieldLane++;
    };
    PDN->getSecondaryButton()->setButtonPress(downCb, this, ButtonInteraction::CLICK);

    // Chase LED animation
    PDN->getLightManager()->startAnimation({
        AnimationType::VERTICAL_CHASE, true, 4, EaseCurve::LINEAR,
        LEDState(), 0
    });

    // Start threat advancement timer
    threatTimer.setTimer(game->getConfig().threatSpeedMs);
}

void BreachDefenseGameplay::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    if (threatTimer.expired()) {
        session.threatPosition++;

        if (session.threatPosition >= config.threatDistance) {
            session.threatArrived = true;
            transitionToEvaluateState = true;
        } else {
            // Reset timer for next step
            threatTimer.setTimer(config.threatSpeedMs);
        }
    }
}

void BreachDefenseGameplay::onStateDismounted(Device* PDN) {
    threatTimer.invalidate();
    transitionToEvaluateState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool BreachDefenseGameplay::transitionToEvaluate() {
    return transitionToEvaluateState;
}
