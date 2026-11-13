#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"

#define MATCH_PUSHED_TAG "MATCH_PUSHED"

MatchPushed::MatchPushed(const GameContext& ctx)
    : TypedState<PDN>(MATCH_PUSHED)
    , matchManager(ctx.matchManager) {}

MatchPushed::~MatchPushed() {
    LOG_I(MATCH_PUSHED_TAG, "MatchPushed state destroyed");
    this->matchManager = nullptr;
}

void MatchPushed::onStateMounted(PDN* pdn) {
    LOG_I(MATCH_PUSHED_TAG, "MatchPushed state mounted");

    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();

    gracePeriodTimer.setTimer(MATCH_RESULT_GRACE_PERIOD);

    pdn->getHaptics()->setIntensity(0);
}

void MatchPushed::onStateLoop(PDN* pdn) { gracePeriodTimer.updateTime(); }

void MatchPushed::onStateDismounted(PDN* pdn) {
    LOG_I(MATCH_PUSHED_TAG, "MatchPushed state dismounted");

    gracePeriodTimer.invalidate();
}

bool MatchPushed::transitionToMatchResult() {
    return matchManager->matchResultsAreIn() || gracePeriodTimer.expired();
}