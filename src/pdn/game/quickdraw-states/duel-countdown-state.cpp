#include "device/drivers/serial-wrapper.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/countdown-animation.hpp"
#include "game/chain-duel-manager.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

DuelCountdown::DuelCountdown(const GameContext& ctx)
    : ConnectState<PDN>(ctx.remoteDeviceCoordinator, DUEL_COUNTDOWN) {
    this->player = ctx.player;
    this->matchManager = ctx.matchManager;
    this->chainDuelManager = ctx.chainDuelManager;
}

DuelCountdown::~DuelCountdown() {
    player = nullptr;
    matchManager = nullptr;
}


void DuelCountdown::onStateMounted(PDN* pdn) {
    // If this device is a champion, tell its supporter chain that the
    // duel is starting so they can arm their confirmation window.
    chainDuelManager->sendGameEventToSupporters(ChainGameEventType::COUNTDOWN);

    pdn->getDisplay()->
    invalidateScreen()->
    drawImage(getImageForAllegiance(player->getAllegiance(), getImageIdForStep(countdownQueue[currentStepIndex].step)))->
    render();

    pdn->getLightManager()->startAnimation(new CountdownAnimation(), countdownQueue[currentStepIndex].animationConfig);

    countdownTimer.setTimer(countdownQueue[currentStepIndex].countdownTimer);
    currentStepIndex++;

    pdn->getPrimaryButton()->setButtonPress(
        matchManager->getButtonMasher(),
        matchManager, ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress(
        matchManager->getButtonMasher(),
        matchManager);

    pdn->getHaptics()->setIntensity(HAPTIC_INTENSITY);
    hapticTimer.setTimer(HAPTIC_DURATION);
}


void DuelCountdown::onStateLoop(PDN* pdn) {
    countdownTimer.updateTime();
    hapticTimer.updateTime();

    if (hapticTimer.expired()) {
        pdn->getHaptics()->setIntensity(0);
    }

    if (countdownTimer.expired()) {
        pdn->getHaptics()->setIntensity(HAPTIC_INTENSITY);
        hapticTimer.setTimer(HAPTIC_DURATION);
        if(countdownQueue[currentStepIndex].step == CountdownStep::BATTLE) {
            doBattle = true;
        } else {
            pdn->getDisplay()->
            invalidateScreen()->
            drawImage(getImageForAllegiance(player->getAllegiance(), getImageIdForStep(countdownQueue[currentStepIndex].step)))->
            render();

            pdn->getLightManager()->startAnimation(new CountdownAnimation(), countdownQueue[currentStepIndex].animationConfig);

            countdownTimer.setTimer(countdownQueue[currentStepIndex].countdownTimer);
            currentStepIndex++;
        }
    }
}

ImageType DuelCountdown::getImageIdForStep(CountdownStep step) {
    switch(step) {
        case CountdownStep::THREE:
            return ImageType::COUNTDOWN_THREE;
        case CountdownStep::TWO:
            return ImageType::COUNTDOWN_TWO;
        default:
            return ImageType::COUNTDOWN_ONE;
    }
}


void DuelCountdown::onStateDismounted(PDN* pdn) {
    if (!doBattle) {
        matchManager->clearCurrentMatch();
        // Countdown aborted (opponent unplugged). Tell supporters to disarm
        // so they don't stay stuck on "PRESS".
        if (chainDuelManager != nullptr) {
            chainDuelManager->sendGameEventToSupporters(ChainGameEventType::DRAW);
        }
    }

    doBattle = false;
    currentStepIndex = 0;
    countdownTimer.invalidate();
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
}

bool DuelCountdown::shallWeBattle() {
    return doBattle;
}

bool DuelCountdown::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool DuelCountdown::isPrimaryRequired() {
    return player->isHunter();
}

bool DuelCountdown::isAuxRequired() {
    return !player->isHunter();
}