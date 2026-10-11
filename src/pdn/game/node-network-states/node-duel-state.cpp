#include "game/node-network-states.hpp"
#include "game/node-network-resources.hpp"
#include "device/animation/countdown-animation.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

#define NODE_DUEL_TAG "NODE_DUEL"

NodeDuel::NodeDuel(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, NodeNetworkStateId::DUEL) {
    this->player = player;
    this->matchManager = matchManager;
}

NodeDuel::~NodeDuel() {
    player = nullptr;
    matchManager = nullptr;
}

void NodeDuel::onStateMounted(PDN* pdn) {
    matchManager->setDuelLocalStartTime(SimpleTimer::getPlatformClock()->milliseconds());

    parameterizedCallbackFunction duelButtonPush = matchManager->getDuelButtonPush();
    pdn->getPrimaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);

    duelTimer.setTimer(DUEL_TIMEOUT);

    pdn->getDisplay()->invalidateScreen()->
        drawImage(getNodeImage(ImageType::IDLE))->
        drawImage(getNodeImage(ImageType::DRAW))->
        render();

    AnimationConfig config;
    config.speed = 16;
    config.loopDelayMs = 0;
    config.loop = false;
    config.initialState = COUNTDOWN_DUEL_STATE;
    pdn->getLightManager()->startAnimation(new CountdownAnimation(), config);
    pdn->getHaptics()->setIntensity(175);
}

void NodeDuel::onStateLoop(PDN* pdn) {
    duelTimer.updateTime();

    if (matchManager->getHasReceivedDrawResult()) {
        transitionToDuelReceivedResultState = true;
        return;
    }
    if (matchManager->getHasPressedButton()) {
        transitionToDuelPushedState = true;
        return;
    }

    if (!duelTimer.expired() && isConnected()) {
        return;
    }
    transitionToIdleState = true;
}

bool NodeDuel::transitionToIdle() {
    return transitionToIdleState;
}

bool NodeDuel::transitionToDuelPushed() {
    return transitionToDuelPushedState;
}

bool NodeDuel::transitionToDuelReceivedResult() {
    return transitionToDuelReceivedResultState;
}

void NodeDuel::onStateDismounted(PDN* pdn) {
    if (transitionToIdleState) {
        pdn->getHaptics()->off();
        matchManager->clearCurrentMatch();
        pdn->getPrimaryButton()->removeButtonCallbacks();
        pdn->getSecondaryButton()->removeButtonCallbacks();
    } else if (transitionToDuelReceivedResultState) {
        pdn->getPrimaryButton()->removeButtonCallbacks();
        pdn->getSecondaryButton()->removeButtonCallbacks();
    }

    duelTimer.invalidate();
    transitionToDuelReceivedResultState = false;
    transitionToIdleState = false;
    transitionToDuelPushedState = false;
}

bool NodeDuel::isPrimaryRequired() {
    return matchManager->localSideIsInitiator();
}

bool NodeDuel::isAuxRequired() {
    return !matchManager->localSideIsInitiator();
}
