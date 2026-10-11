#include "game/node-network-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

NodeDuelReceivedResult::NodeDuelReceivedResult(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, NodeNetworkStateId::DUEL_RECEIVED_RESULT) {
    this->player = player;
    this->matchManager = matchManager;
}

NodeDuelReceivedResult::~NodeDuelReceivedResult() {
    player = nullptr;
    matchManager = nullptr;
}

void NodeDuelReceivedResult::onStateMounted(PDN* pdn) {
    parameterizedCallbackFunction duelButtonPush = matchManager->getDuelButtonPush();
    pdn->getPrimaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);
    buttonPushGraceTimer.setTimer(BUTTON_PUSH_GRACE_PERIOD);
}

void NodeDuelReceivedResult::onStateLoop(PDN* pdn) {
    if (matchManager->getHasPressedButton()) {
        pdn->getHaptics()->setIntensity(0);
    }

    buttonPushGraceTimer.updateTime();
    if (buttonPushGraceTimer.expired()) {
        unsigned long pityTime = SimpleTimer::getPlatformClock()->milliseconds() - matchManager->getDuelLocalStartTime();
        matchManager->sendNeverPressed(pityTime);
        transitionToDuelResultState = true;
    }
}

void NodeDuelReceivedResult::onStateDismounted(PDN* pdn) {
    if (!isConnected()) {
        matchManager->clearCurrentMatch();
    }
    transitionToDuelResultState = false;
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    buttonPushGraceTimer.invalidate();
}

bool NodeDuelReceivedResult::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || transitionToDuelResultState;
}

bool NodeDuelReceivedResult::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool NodeDuelReceivedResult::isPrimaryRequired() {
    return matchManager->localSideIsInitiator();
}

bool NodeDuelReceivedResult::isAuxRequired() {
    return !matchManager->localSideIsInitiator();
}
