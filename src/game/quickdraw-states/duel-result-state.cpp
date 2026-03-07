#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "game/chain-boost.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"
#include "device/serial-manager.hpp"

#define DUEL_RESULT_TAG "DUEL_RESULT"

DuelResult::DuelResult(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager, ChainContext* chainContext) : State(QuickdrawStateId::DUEL_RESULT) {
    this->player = player;
    this->matchManager = matchManager;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
    this->chainContext_ = chainContext;
}

DuelResult::~DuelResult() {
    LOG_I(DUEL_RESULT_TAG, "Duel result state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
    this->quickdrawWirelessManager = nullptr;
}

void DuelResult::onStateMounted(Device *PDN) {
    LOG_I(DUEL_RESULT_TAG, "Duel result state mounted");

    if (chainContext_) {
        int boost = calculateBoost(chainContext_->confirmedSupporters);
        if (boost > 0) {
            Match* match = matchManager->getMatch();
            if (match) {
                if (player->isHunter()) {
                    unsigned long t = match->getHunterDrawTime();
                    match->setHunterDrawTime(t > (unsigned long)boost ? t - boost : 0);
                } else {
                    unsigned long t = match->getBountyDrawTime();
                    match->setBountyDrawTime(t > (unsigned long)boost ? t - boost : 0);
                }
            }
        }
    }

    player->incrementMatchesPlayed();

    bool didWin = matchManager->didWin();

    if(didWin) {
        wonBattle = true;
        player->incrementWins();
        player->incrementStreak();
    } else {
        captured = true;
        player->resetStreak();
        player->incrementLosses();
    }

    if (chainContext_ && chainContext_->chainLength > 1) {
        const char* resultEvent = didWin ? "event:win" : "event:loss";
        PDN->getSerialManager()->writeString(resultEvent, SerialIdentifier::INPUT_JACK);
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