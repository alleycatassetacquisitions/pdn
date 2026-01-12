#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include "logger.hpp"

#define DUEL_RESULT_RECEIVED_TAG "DUEL_RESULT_RECEIVED"

DuelReceivedResult::DuelReceivedResult(Player* player, MatchManager* matchManager) : State(DUEL_RECEIVED_RESULT) {
    this->player = player;
    this->matchManager = matchManager;
}

DuelReceivedResult::~DuelReceivedResult() {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state destroyed");
    player = nullptr;
    matchManager = nullptr;
}

void DuelReceivedResult::onStateMounted(Device *PDN) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state mounted");
    buttonPushGraceTimer.setTimer(BUTTON_PUSH_GRACE_PERIOD);

    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

void DuelReceivedResult::onStateLoop(Device *PDN) {
    if(matchManager->getHasPressedButton()) {
        PDN->setVibration(0);
    }

    buttonPushGraceTimer.updateTime();

    if(buttonPushGraceTimer.expired()) {
        LOG_I(DUEL_RESULT_RECEIVED_TAG, "Button push grace period expired");

        matchManager->setNeverPressed();

        unsigned long pityTime = millis() - matchManager->getDuelLocalStartTime();

        player->isHunter() ? 
        matchManager->setHunterDrawTime(pityTime) 
        : matchManager->setBountyDrawTime(pityTime);
        
        QuickdrawWirelessManager::GetInstance()->broadcastPacket(
            player->getOpponentMacAddress()->c_str(),
            QDCommand::NEVER_PRESSED,
            *matchManager->getCurrentMatch()
        );
        transitionToDuelResultState = true;
    }
}   

void DuelReceivedResult::onStateDismounted(Device *PDN) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state dismounted");
    transitionToDuelResultState = false;
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
    buttonPushGraceTimer.invalidate();
}

bool DuelReceivedResult::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || transitionToDuelResultState;
}


