
#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"

#define DUEL_RESULT_RECEIVED_TAG "DUEL_RESULT_RECEIVED"

DuelReceivedResult::DuelReceivedResult(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainContext* chainContext) : ConnectState(remoteDeviceCoordinator, DUEL_RECEIVED_RESULT) {
    this->player = player;
    this->matchManager = matchManager;
    this->chainContext_ = chainContext;
}

DuelReceivedResult::~DuelReceivedResult() {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
}

void DuelReceivedResult::onStateMounted(Device *PDN) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state mounted");

    auto duelButtonPush = matchManager->getDuelButtonPush();
    PDN->getPrimaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);

    buttonPushGraceTimer.setTimer(BUTTON_PUSH_GRACE_PERIOD);
}

void DuelReceivedResult::onStateLoop(Device *PDN) {
    if(matchManager->getHasPressedButton()) {
        PDN->getHaptics()->setIntensity(0);
    }

    buttonPushGraceTimer.updateTime();

    if(buttonPushGraceTimer.expired()) {
        LOG_I(DUEL_RESULT_RECEIVED_TAG, "Button push grace period expired");

        unsigned long pityTime = SimpleTimer::getPlatformClock()->milliseconds() - matchManager->getDuelLocalStartTime();

        matchManager->sendNeverPressed(pityTime);
        transitionToDuelResultState = true;
    }
}   

void DuelReceivedResult::onStateDismounted(Device *PDN) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state dismounted");

    if (!isConnected()) {
        matchManager->clearCurrentMatch();
    }

    transitionToDuelResultState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    buttonPushGraceTimer.invalidate();
}

bool DuelReceivedResult::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || transitionToDuelResultState;
}

bool DuelReceivedResult::disconnectedBackToIdle() {
    return !isConnected();
}

bool DuelReceivedResult::isPrimaryRequired() {
    return player->isHunter();
}

bool DuelReceivedResult::isAuxRequired() {
    return !player->isHunter();
}