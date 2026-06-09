#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"

#define DUEL_PUSHED_TAG "DUEL_PUSHED"

DuelPushed::DuelPushed(MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : DuelWaitState(matchManager, remoteDeviceCoordinator, DUEL_PUSHED) {
}

DuelPushed::~DuelPushed() {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state destroyed");
    this->matchManager = nullptr;
}

void DuelPushed::onStateMounted(PDN* pdn) {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state mounted");
    
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();

    gracePeriodTimer.setTimer(quickdraw_timing::DUEL_RESULT_GRACE_PERIOD_MS);

    pdn->getHaptics()->setIntensity(0);
}

void DuelPushed::onStateLoop(PDN* pdn) {
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

void DuelPushed::onStateDismounted(PDN* pdn) {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state dismounted");

    clearMatchOnDebouncedDisconnect();

    gracePeriodTimer.invalidate();
}

bool DuelPushed::isPrimaryRequired() {
    return matchManager->localIsHunterForMatch();
}

bool DuelPushed::isAuxRequired() {
    return !matchManager->localIsHunterForMatch();
}

bool DuelPushed::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || gracePeriodTimer.expired();
}