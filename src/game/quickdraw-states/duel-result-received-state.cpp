
#include "game/quickdraw-states.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"

#define DUEL_RESULT_RECEIVED_TAG "DUEL_RESULT_RECEIVED"

DuelReceivedResult::DuelReceivedResult(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager) : State(DUEL_RECEIVED_RESULT) {
    this->player = player;
    this->matchManager = matchManager;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
}

DuelReceivedResult::~DuelReceivedResult() {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state destroyed");
    player = nullptr;
    matchManager = nullptr;
    quickdrawWirelessManager = nullptr;
}

void DuelReceivedResult::onStateMounted(Device *PDN) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state mounted");
    buttonPushGraceTimer.setTimer(BUTTON_PUSH_GRACE_PERIOD);

    quickdrawWirelessManager->clearCallbacks();
}

void DuelReceivedResult::onStateLoop(Device *PDN) {
    if(matchManager->getHasPressedButton()) {
        PDN->getHaptics()->setIntensity(0);
    }

    buttonPushGraceTimer.updateTime();

    if(buttonPushGraceTimer.expired()) {
        LOG_I(DUEL_RESULT_RECEIVED_TAG, "Button push grace period expired");

        matchManager->setNeverPressed();

        unsigned long pityTime = SimpleTimer::getPlatformClock()->milliseconds() - matchManager->getDuelLocalStartTime();

        player->isHunter() ? 
        matchManager->setHunterDrawTime(pityTime) 
        : matchManager->setBountyDrawTime(pityTime);
        
        quickdrawWirelessManager->broadcastPacket(
            player->getOpponentMacAddress(),
            QDCommand::NEVER_PRESSED,
            *matchManager->getCurrentMatch()
        );
        transitionToDuelResultState = true;
    }
}   

void DuelReceivedResult::onStateDismounted(Device *PDN) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state dismounted");
    transitionToDuelResultState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    buttonPushGraceTimer.invalidate();
}

bool DuelReceivedResult::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || transitionToDuelResultState;
}


