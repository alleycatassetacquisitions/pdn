#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"

#define DUEL_PUSHED_TAG "DUEL_PUSHED"

DuelPushed::DuelPushed(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) : ConnectState<PDN>(remoteDeviceCoordinator, DUEL_PUSHED) {
    this->player = player;
    this->matchManager = matchManager;
}

DuelPushed::~DuelPushed() {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
}

void DuelPushed::onStateMounted(PDN* pdn) {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state mounted");
    
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();

    gracePeriodTimer.setTimer(DUEL_RESULT_GRACE_PERIOD);

    pdn->getHaptics()->setIntensity(0);
}

void DuelPushed::onStateLoop(PDN* pdn) {
    gracePeriodTimer.updateTime();
}

void DuelPushed::onStateDismounted(PDN* pdn) {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state dismounted");

    if (!isConnected()) {
        matchManager->clearCurrentMatch();
    }

    gracePeriodTimer.invalidate();
}

bool DuelPushed::isPrimaryRequired() {
    return player->isHunter();
}

bool DuelPushed::isAuxRequired() {
    return !player->isHunter();
}

bool DuelPushed::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool DuelPushed::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || gracePeriodTimer.expired();
}