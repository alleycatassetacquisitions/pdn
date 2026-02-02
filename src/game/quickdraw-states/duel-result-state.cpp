#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"

#define DUEL_RESULT_TAG "DUEL_RESULT"

DuelResult::DuelResult(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager) : State(QuickdrawStateId::DUEL_RESULT), player(player), matchManager(matchManager) {
    this->matchManager = matchManager;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
}

DuelResult::~DuelResult() {
    LOG_I(DUEL_RESULT_TAG, "Duel result state destroyed");
    player = nullptr;
    matchManager = nullptr;
    quickdrawWirelessManager = nullptr;
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

    // Store reaction time before finalizing match
    if(player->isHunter()) {
        player->addReactionTime(matchManager->getCurrentMatch()->getHunterDrawTime());
    } else {
        player->addReactionTime(matchManager->getCurrentMatch()->getBountyDrawTime());
    }

    // Now it's safe to finalize the match, which might clear the current match
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

    quickdrawWirelessManager->clearCallbacks();
             
    wonBattle = false;
    captured = false;
    PDN->getLightManager()->stopAnimation();
}

bool DuelResult::transitionToWin() {
    if (wonBattle) {
        LOG_I(DUEL_RESULT_TAG, "Transitioning to win state");
    }
    return wonBattle;
}

bool DuelResult::transitionToLose() {
    if (captured) {
        LOG_I(DUEL_RESULT_TAG, "Transitioning to lose state");
    }
    return captured;
}

