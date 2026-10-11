#include "game/node-network-states.hpp"
#include "game/node-network-resources.hpp"
#include "device/animation/countdown-animation.hpp"
#include "device/device.hpp"

NodeDuelCountdown::CountdownStage::CountdownStage(CountdownStep step, unsigned long countdownTimerMs) {
    this->step = step;
    this->countdownTimer = countdownTimerMs;
    animationConfig.loop = false;
    animationConfig.speed = 16;
    if (step == CountdownStep::THREE) {
        animationConfig.initialState = COUNTDOWN_THREE_STATE;
    } else if (step == CountdownStep::TWO) {
        animationConfig.initialState = COUNTDOWN_TWO_STATE;
    } else if (step == CountdownStep::ONE) {
        animationConfig.initialState = COUNTDOWN_ONE_STATE;
    } else {
        animationConfig.initialState = COUNTDOWN_DUEL_STATE;
    }
}

NodeDuelCountdown::NodeDuelCountdown(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, NodeNetworkStateId::DUEL_COUNTDOWN) {
    this->player = player;
    this->matchManager = matchManager;
}

NodeDuelCountdown::~NodeDuelCountdown() {
    player = nullptr;
    matchManager = nullptr;
}

void NodeDuelCountdown::onStateMounted(PDN* pdn) {
    static const CountdownStage queue[4] = {
        CountdownStage(CountdownStep::THREE, 2000),
        CountdownStage(CountdownStep::TWO, 2000),
        CountdownStage(CountdownStep::ONE, 2000),
        CountdownStage(CountdownStep::BATTLE, 0),
    };

    pdn->getDisplay()->
        invalidateScreen()->
        drawImage(getNodeImage(getImageIdForStep(queue[currentStepIndex].step)))->
        render();

    pdn->getLightManager()->startAnimation(new CountdownAnimation(), queue[currentStepIndex].animationConfig);
    countdownTimer.setTimer(queue[currentStepIndex].countdownTimer);
    currentStepIndex++;

    pdn->getPrimaryButton()->setButtonPress(matchManager->getButtonMasher(), matchManager, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(matchManager->getButtonMasher(), matchManager, ButtonInteraction::CLICK);

    pdn->getHaptics()->setIntensity(HAPTIC_INTENSITY);
    hapticTimer.setTimer(HAPTIC_DURATION);
}

void NodeDuelCountdown::onStateLoop(PDN* pdn) {
    static const CountdownStage queue[4] = {
        CountdownStage(CountdownStep::THREE, 2000),
        CountdownStage(CountdownStep::TWO, 2000),
        CountdownStage(CountdownStep::ONE, 2000),
        CountdownStage(CountdownStep::BATTLE, 0),
    };

    countdownTimer.updateTime();
    hapticTimer.updateTime();

    if (hapticTimer.expired()) {
        pdn->getHaptics()->setIntensity(0);
    }

    if (countdownTimer.expired()) {
        pdn->getHaptics()->setIntensity(HAPTIC_INTENSITY);
        hapticTimer.setTimer(HAPTIC_DURATION);
        if (currentStepIndex >= 4 || queue[currentStepIndex].step == CountdownStep::BATTLE) {
            doBattle = true;
        } else {
            pdn->getDisplay()->
                invalidateScreen()->
                drawImage(getNodeImage(getImageIdForStep(queue[currentStepIndex].step)))->
                render();
            pdn->getLightManager()->startAnimation(new CountdownAnimation(), queue[currentStepIndex].animationConfig);
            countdownTimer.setTimer(queue[currentStepIndex].countdownTimer);
            currentStepIndex++;
        }
    }
}

ImageType NodeDuelCountdown::getImageIdForStep(CountdownStep step) {
    switch (step) {
        case CountdownStep::THREE:
            return ImageType::COUNTDOWN_THREE;
        case CountdownStep::TWO:
            return ImageType::COUNTDOWN_TWO;
        default:
            return ImageType::COUNTDOWN_ONE;
    }
}

void NodeDuelCountdown::onStateDismounted(PDN* pdn) {
    if (!doBattle) {
        matchManager->clearCurrentMatch();
    }
    doBattle = false;
    currentStepIndex = 0;
    countdownTimer.invalidate();
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
}

bool NodeDuelCountdown::shallWeBattle() {
    return doBattle;
}

bool NodeDuelCountdown::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool NodeDuelCountdown::isPrimaryRequired() {
    return matchManager->localSideIsInitiator();
}

bool NodeDuelCountdown::isAuxRequired() {
    return !matchManager->localSideIsInitiator();
}
