#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/countdown-animation.hpp"
#include "game/match-manager.hpp"
#include "game/posse-manager.hpp"
#include "game/shootout-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"

#define MATCH_TAG "MATCH_STATE"

MatchState::MatchState(const GameContext& ctx)
    : TypedState<PDN>(MATCH)
    , player(ctx.player)
    , matchManager(ctx.matchManager)
    , posseManager(ctx.posseManager)
    , shootoutManager(ctx.shootoutManager) {}

void MatchState::onStateMounted(PDN* pdn) {
    LOG_I(MATCH_TAG, "Match state mounted");

    // Disarm the supporter confirmation window now that the draw phase has begun.
    posseManager->sendGameEventToSupporters(ChainGameEventType::DRAW);

    matchManager->setMatchLocalStartTime(SimpleTimer::getPlatformClock()->milliseconds());

    LOG_I(MATCH_TAG, "Setting up button handlers");

    parameterizedCallbackFunction matchButtonPush = matchManager->getMatchButtonPush();
    pdn->getPrimaryButton()->setButtonPress(matchButtonPush, matchManager,
                                            ButtonInteraction::PRESS);
    pdn->getSecondaryButton()->setButtonPress(matchButtonPush, matchManager,
                                              ButtonInteraction::PRESS);

    matchTimer.setTimer(MATCH_TIMEOUT);

    LOG_I(MATCH_TAG, "Match timer started for %d ms, matchStartTime: %lu", MATCH_TIMEOUT,
          matchManager->getMatchLocalStartTime());

    pdn->getDisplay()
    ->invalidateScreen()
    ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))
    ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::DRAW))
    ->render();

    LOG_I(MATCH_TAG, "Draw image displayed for allegiance: %d", player->getAllegiance());

    AnimationConfig config;
    config.speed = 16;
    config.loopDelayMs = 0;
    config.loop = false;
    config.initialState = COUNTDOWN_MATCH_STATE;

    pdn->getLightManager()->startAnimation(new CountdownAnimation(), config);

    pdn->getHaptics()->setIntensity(175);
}

void MatchState::onStateLoop(PDN* pdn) {
    matchTimer.updateTime();

    if (matchManager->getHasReceivedDrawResult()) {
        transitionToMatchReceivedResultState = true;
        return;
    } else if (matchManager->getHasPressedButton()) {
        transitionToMatchPushedState = true;
        return;
    }

    if (!matchTimer.expired()) return;

    if (!shootoutManager || !shootoutManager->active()) {
        transitionToIdleState = true;
        return;
    }

    // Shootout timeout: the match's hunter forfeits, its bounty wins. The draw
    // slot decides, not the standing role — a shootout pairs by MAC ordering, so
    // the two disagree for the whole match.
    if (matchManager->isLocalHunter()) {
        transitionToShootoutEliminatedState = true;
        return;
    }
    shootoutManager->reportLocalWin();
    transitionToShootoutSpectatorState = true;
}

bool MatchState::transitionToIdle() const { return transitionToIdleState; }

bool MatchState::transitionToMatchPushed() const {
    if (transitionToMatchPushedState) {
        LOG_I(MATCH_TAG, "Transitioning to match pushed state");
    }
    return transitionToMatchPushedState;
}

bool MatchState::transitionToMatchReceivedResult() const {
    if (transitionToMatchReceivedResultState) {
        LOG_I(MATCH_TAG, "Transitioning to match result state");
    }
    return transitionToMatchReceivedResultState;
}

bool MatchState::transitionToShootoutSpectator() const {
    return transitionToShootoutSpectatorState;
}

bool MatchState::transitionToShootoutEliminated() const {
    return transitionToShootoutEliminatedState;
}

void MatchState::onStateDismounted(PDN* pdn) {
    // Input goes on every exit. No successor inherits these callbacks — each
    // registers what it wants on mount, or wants none.
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();

    LOG_I(MATCH_TAG, "Match state dismounted - Cleanup");

    matchTimer.invalidate();
    LOG_I(MATCH_TAG, "Match timer invalidated");

    transitionToMatchReceivedResultState = false;
    transitionToIdleState = false;
    transitionToMatchPushedState = false;
    transitionToShootoutSpectatorState = false;
    transitionToShootoutEliminatedState = false;
}