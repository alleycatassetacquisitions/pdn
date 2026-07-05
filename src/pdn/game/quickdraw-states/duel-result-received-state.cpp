
#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"

#define DUEL_RESULT_RECEIVED_TAG "DUEL_RESULT_RECEIVED"

DuelReceivedResult::DuelReceivedResult(const GameContext& ctx)
    : ConnectState<PDN>(ctx.remoteDeviceCoordinator, DUEL_RECEIVED_RESULT) {
    this->player = ctx.player;
    this->matchManager = ctx.matchManager;
}

DuelReceivedResult::~DuelReceivedResult() {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
}

void DuelReceivedResult::onStateMounted(PDN* pdn) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state mounted");

    auto duelButtonPush = matchManager->getDuelButtonPush();
    pdn->getPrimaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);

    buttonPushGraceTimer.setTimer(BUTTON_PUSH_GRACE_PERIOD);
}

void DuelReceivedResult::onStateLoop(PDN* pdn) {
    if(matchManager->getHasPressedButton()) {
        pdn->getHaptics()->setIntensity(0);
    }

    buttonPushGraceTimer.updateTime();

    if(buttonPushGraceTimer.expired()) {
        LOG_I(DUEL_RESULT_RECEIVED_TAG, "Button push grace period expired");

        unsigned long pityTime = SimpleTimer::getPlatformClock()->milliseconds() - matchManager->getDuelLocalStartTime();

        matchManager->sendNeverPressed(pityTime);
        transitionToDuelResultState = true;
    }
}   

void DuelReceivedResult::onStateDismounted(PDN* pdn) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state dismounted");

    if (!isConnected()) {
        matchManager->clearCurrentMatch();
    }

    transitionToDuelResultState = false;
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    buttonPushGraceTimer.invalidate();
}

bool DuelReceivedResult::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || transitionToDuelResultState;
}

bool DuelReceivedResult::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool DuelReceivedResult::isPrimaryRequired() {
    return player->isHunter();
}

bool DuelReceivedResult::isAuxRequired() {
    return !player->isHunter();
}