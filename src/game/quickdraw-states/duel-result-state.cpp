#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"

#define DUEL_RESULT_TAG "DUEL_RESULT"

DuelResult::DuelResult(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager, ShootoutManager* shootoutManager) : State(QuickdrawStateId::DUEL_RESULT) {
    this->player = player;
    this->matchManager = matchManager;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
    this->shootoutManager = shootoutManager;
}

DuelResult::~DuelResult() {
    LOG_I(DUEL_RESULT_TAG, "Duel result state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
    this->quickdrawWirelessManager = nullptr;
    this->shootoutManager = nullptr;
}

void DuelResult::onStateMounted(Device *PDN) {
    LOG_I(DUEL_RESULT_TAG, "Duel result state mounted");

    player->incrementMatchesPlayed();

    if(matchManager->didWin()) {
        wonBattle = true;
        player->incrementWins();
        player->incrementStreak();
    } else {
        captured = true;
        player->resetStreak();
        player->incrementLosses();
    }

    PDN->getHaptics()->setIntensity(0);

    matchManager->finalizeMatch();

    PDN->getDisplay()->invalidateScreen()->render();
}

void DuelResult::onStateLoop(Device *PDN) {
    // No loop processing needed for result state
}

void DuelResult::onStateDismounted(Device *PDN) {
    LOG_I(DUEL_RESULT_TAG, "Duel result state dismounted - Cleaning up");
    
    // Log state before reset
    LOG_I(DUEL_RESULT_TAG, "State before reset - wonBattle: %d, captured: %d",
             wonBattle, captured);

    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();

    wonBattle = false;
    captured = false;
    PDN->getLightManager()->stopAnimation();
}

bool DuelResult::transitionToWin() {
    if (shootoutManager && shootoutManager->active()) return false;
    if (wonBattle) {
        LOG_I(DUEL_RESULT_TAG, "Transitioning to win state");
    }
    return wonBattle;
}

bool DuelResult::transitionToLose() {
    if (shootoutManager && shootoutManager->active()) return false;
    if (captured) {
        LOG_I(DUEL_RESULT_TAG, "Transitioning to lose state");
    }
    return captured;
}

bool DuelResult::transitionToShootoutSpectator() {
    if (!shootoutManager || !shootoutManager->active()) return false;
    if (!wonBattle) return false;
    shootoutManager->reportLocalWin();
    return true;
}

bool DuelResult::transitionToShootoutEliminated() {
    if (!shootoutManager || !shootoutManager->active()) return false;
    return !wonBattle;
}