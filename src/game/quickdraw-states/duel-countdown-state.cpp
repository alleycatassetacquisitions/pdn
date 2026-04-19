#include "device/drivers/serial-wrapper.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/chain-duel-manager.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

DuelCountdown::DuelCountdown(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainDuelManager* chainDuelManager) : ConnectState(remoteDeviceCoordinator, DUEL_COUNTDOWN) {
    this->player = player;
    this->matchManager = matchManager;
    this->chainDuelManager = chainDuelManager;
}

DuelCountdown::~DuelCountdown() {
    player = nullptr;
    matchManager = nullptr;
}


void DuelCountdown::onStateMounted(Device *PDN) {
    // If this device is a champion, tell its supporter chain that the
    // duel is starting so they can arm their confirmation window.
    chainDuelManager->sendGameEventToSupporters(ChainGameEventType::COUNTDOWN);

    PDN->getDisplay()->
    invalidateScreen()->
    drawImage(getImageForAllegiance(player->getAllegiance(), getImageIdForStep(countdownQueue[currentStepIndex].step)))->
    render();

    PDN->getLightManager()->startAnimation(countdownQueue[currentStepIndex].animationConfig);

    countdownTimer.setTimer(countdownQueue[currentStepIndex].countdownTimer);
    currentStepIndex++;

    PDN->getPrimaryButton()->setButtonPress(
        matchManager->getButtonMasher(),
        matchManager, ButtonInteraction::CLICK);

    PDN->getSecondaryButton()->setButtonPress(
        matchManager->getButtonMasher(),
        matchManager);

    PDN->getHaptics()->setIntensity(HAPTIC_INTENSITY);
    hapticTimer.setTimer(HAPTIC_DURATION);
}


void DuelCountdown::onStateLoop(Device *PDN) {
    countdownTimer.updateTime();
    hapticTimer.updateTime();

    if (hapticTimer.expired()) {
        PDN->getHaptics()->setIntensity(0);
    }

    if (countdownTimer.expired()) {
        PDN->getHaptics()->setIntensity(HAPTIC_INTENSITY);
        hapticTimer.setTimer(HAPTIC_DURATION);
        if(countdownQueue[currentStepIndex].step == CountdownStep::BATTLE) {
            doBattle = true;
        } else {
            PDN->getDisplay()->
            invalidateScreen()->
            drawImage(getImageForAllegiance(player->getAllegiance(), getImageIdForStep(countdownQueue[currentStepIndex].step)))->
            render();

            PDN->getLightManager()->startAnimation(countdownQueue[currentStepIndex].animationConfig);

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


void DuelCountdown::onStateDismounted(Device *PDN) {
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
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool DuelCountdown::shallWeBattle() {
    return doBattle;
}

bool DuelCountdown::disconnectedBackToIdle() {
    return !isConnected();
}

bool DuelCountdown::isPrimaryRequired() {
    return player->isHunter();
}

bool DuelCountdown::isAuxRequired() {
    return !player->isHunter();
}