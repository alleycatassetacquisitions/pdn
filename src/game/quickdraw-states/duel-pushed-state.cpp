#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"

#define DUEL_PUSHED_TAG "DUEL_PUSHED"

DuelPushed::DuelPushed(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) : ConnectState(remoteDeviceCoordinator, DUEL_PUSHED) {
    this->player = player;
    this->matchManager = matchManager;
}

DuelPushed::~DuelPushed() {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
}

void DuelPushed::onStateMounted(Device *PDN) {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state mounted");
    
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();

    gracePeriodTimer.setTimer(DUEL_RESULT_GRACE_PERIOD);

    PDN->getHaptics()->setIntensity(0);
}

void DuelPushed::onStateLoop(Device *PDN) {
    gracePeriodTimer.updateTime();

    // Grace ran out with no result; void to avoid resolving against a
    // default-zero opponent time. Narrow asymmetric window remains: if our
    // grace expires before the opponent's reliable response arrives, we void
    // while they may persist a real outcome. Server-side match_id dedup
    // catches it; local Player stats can diverge by one match.
    if (gracePeriodTimer.expired() && !matchManager->matchResultsAreIn()) {
        matchManager->voidCurrentMatch();
    }
}

void DuelPushed::onStateDismounted(Device *PDN) {
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