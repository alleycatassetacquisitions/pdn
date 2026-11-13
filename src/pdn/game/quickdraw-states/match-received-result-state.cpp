
#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"

#define MATCH_RECEIVED_RESULT_TAG "MATCH_RECEIVED_RESULT"

MatchReceivedResult::MatchReceivedResult(const GameContext& ctx)
    : TypedState<PDN>(MATCH_RECEIVED_RESULT)
    , matchManager(ctx.matchManager) {}


void MatchReceivedResult::onStateMounted(PDN* pdn) {
    LOG_I(MATCH_RECEIVED_RESULT_TAG, "Match received result state mounted");

    parameterizedCallbackFunction matchButtonPush = matchManager->getMatchButtonPush();
    pdn->getPrimaryButton()->setButtonPress(matchButtonPush, matchManager,
                                            ButtonInteraction::PRESS);
    pdn->getSecondaryButton()->setButtonPress(matchButtonPush, matchManager,
                                              ButtonInteraction::PRESS);

    buttonPushGraceTimer.setTimer(BUTTON_PUSH_GRACE_PERIOD);
}

void MatchReceivedResult::onStateLoop(PDN* pdn) {
    if (matchManager->getHasPressedButton()) {
        pdn->getHaptics()->setIntensity(0);
    }

    buttonPushGraceTimer.updateTime();

    if (buttonPushGraceTimer.expired()) {
        LOG_I(MATCH_RECEIVED_RESULT_TAG, "Button push grace period expired");

        // A press dispatched earlier in this same loop pass has already sent its
        // result; the timeout must not then report the player as never pressing.
        if (!matchManager->getHasPressedButton()) {
            unsigned long pityTime = SimpleTimer::getPlatformClock()->milliseconds() -
                                     matchManager->getMatchLocalStartTime();
            matchManager->sendNeverPressed(pityTime);
        }
        transitionToMatchResultState = true;
    }
}

void MatchReceivedResult::onStateDismounted(PDN* pdn) {
    LOG_I(MATCH_RECEIVED_RESULT_TAG, "Match received result state dismounted");

    transitionToMatchResultState = false;
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    buttonPushGraceTimer.invalidate();
}

bool MatchReceivedResult::transitionToMatchResult() {
    return matchManager->matchResultsAreIn() || transitionToMatchResultState;
}