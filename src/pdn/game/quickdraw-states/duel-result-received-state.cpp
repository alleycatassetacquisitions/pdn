
#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"

#define DUEL_RESULT_RECEIVED_TAG "DUEL_RESULT_RECEIVED"

DuelReceivedResult::DuelReceivedResult(MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : DuelWaitState(matchManager, remoteDeviceCoordinator, DUEL_RECEIVED_RESULT) {
}

DuelReceivedResult::~DuelReceivedResult() {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state destroyed");
    this->matchManager = nullptr;
}

void DuelReceivedResult::onStateMounted(PDN* pdn) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state mounted");

    auto duelButtonPush = matchManager->getDuelButtonPush();
    pdn->getPrimaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);

    buttonPushGraceTimer.setTimer(quickdraw_timing::BUTTON_PUSH_GRACE_PERIOD_MS);
}

void DuelReceivedResult::onStateLoop(PDN* pdn) {
    if(matchManager->getHasPressedButton()) {
        pdn->getHaptics()->setIntensity(0);
    }

    buttonPushGraceTimer.updateTime();

    // sendNeverPressed is first-writer-wins and idempotent, so calling it every
    // tick past expiry needs no "already sent" flag; a press landing the same
    // tick already resolved my side, so it no-ops too.
    if(buttonPushGraceTimer.expired()) {
        unsigned long pityTime = SimpleTimer::getPlatformClock()->milliseconds() - matchManager->getDuelLocalStartTime();
        matchManager->sendNeverPressed(pityTime);
    }
}

void DuelReceivedResult::onStateDismounted(PDN* pdn) {
    LOG_I(DUEL_RESULT_RECEIVED_TAG, "Duel result received state dismounted");

    clearMatchOnDebouncedDisconnect();

    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    buttonPushGraceTimer.invalidate();
}

bool DuelReceivedResult::transitionToDuelResult() {
    return matchManager->matchResultsAreIn();
}

bool DuelReceivedResult::isPrimaryRequired() {
    return matchManager->localIsHunterForMatch();
}

bool DuelReceivedResult::isAuxRequired() {
    return !matchManager->localIsHunterForMatch();
}