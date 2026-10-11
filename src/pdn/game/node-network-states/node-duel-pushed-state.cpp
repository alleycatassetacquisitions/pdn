#include "game/node-network-states.hpp"
#include "device/device.hpp"

NodeDuelPushed::NodeDuelPushed(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, NodeNetworkStateId::DUEL_PUSHED) {
    this->player = player;
    this->matchManager = matchManager;
}

NodeDuelPushed::~NodeDuelPushed() {
    player = nullptr;
    matchManager = nullptr;
}

void NodeDuelPushed::onStateMounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    gracePeriodTimer.setTimer(DUEL_RESULT_GRACE_PERIOD);
    pdn->getHaptics()->setIntensity(0);
}

void NodeDuelPushed::onStateLoop(PDN* pdn) {
    gracePeriodTimer.updateTime();
}

void NodeDuelPushed::onStateDismounted(PDN* pdn) {
    if (!isConnected()) {
        matchManager->clearCurrentMatch();
    }
    gracePeriodTimer.invalidate();
}

bool NodeDuelPushed::isPrimaryRequired() {
    return matchManager->localSideIsInitiator();
}

bool NodeDuelPushed::isAuxRequired() {
    return !matchManager->localSideIsInitiator();
}

bool NodeDuelPushed::disconnectedBackToIdle() {
    return isPersistentlyDisconnected();
}

bool NodeDuelPushed::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || gracePeriodTimer.expired();
}
