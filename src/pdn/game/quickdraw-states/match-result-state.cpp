#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"

#define MATCH_RESULT_TAG "MATCH_RESULT"

MatchResult::MatchResult(const GameContext& ctx)
    : TypedState<PDN>(QuickdrawStateId::MATCH_RESULT) {
    this->player = ctx.player;
    this->matchManager = ctx.matchManager;
    this->shootoutManager = ctx.shootoutManager;
}

MatchResult::~MatchResult() {
    LOG_I(MATCH_RESULT_TAG, "Match result state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
    this->shootoutManager = nullptr;
}

void MatchResult::onStateMounted(PDN* pdn) {
    LOG_I(MATCH_RESULT_TAG, "Match result state mounted");

    player->incrementMatchesPlayed();

    if (matchManager->didWin()) {
        wonBattle = true;
        player->incrementWins();
        player->incrementStreak();
    } else {
        captured = true;
        player->resetStreak();
        player->incrementLosses();
    }

    pdn->getHaptics()->setIntensity(0);

    matchManager->finalizeMatch();

    pdn->getDisplay()->invalidateScreen()->render();
}

void MatchResult::onStateLoop(PDN* pdn) {
    // No loop processing needed for result state
}

void MatchResult::onStateDismounted(PDN* pdn) {
    LOG_I(MATCH_RESULT_TAG, "Match result state dismounted - Cleaning up");
    LOG_I(MATCH_RESULT_TAG, "State before reset - wonBattle: %d, captured: %d", wonBattle,
          captured);

    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();

    wonBattle = false;
    captured = false;
    pdn->getLightManager()->stopAnimation();
}

bool MatchResult::transitionToWin() {
    if (shootoutManager && shootoutManager->active()) return false;
    if (wonBattle) {
        LOG_I(MATCH_RESULT_TAG, "Transitioning to win state");
    }
    return wonBattle;
}

bool MatchResult::transitionToLose() {
    if (shootoutManager && shootoutManager->active()) return false;
    if (captured) {
        LOG_I(MATCH_RESULT_TAG, "Transitioning to lose state");
    }
    return captured;
}

bool MatchResult::transitionToShootoutSpectator() {
    if (!shootoutManager || !shootoutManager->active()) return false;
    if (!wonBattle) return false;
    shootoutManager->reportLocalWin();
    return true;
}

bool MatchResult::transitionToShootoutEliminated() {
    if (!shootoutManager || !shootoutManager->active()) return false;
    return !wonBattle;
}