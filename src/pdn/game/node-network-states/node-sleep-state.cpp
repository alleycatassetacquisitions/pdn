#include "game/node-network-states.hpp"
#include "game/node-network-resources.hpp"
#include "device/device.hpp"

NodeSleep::NodeSleep(Player* player) : TypedState<PDN>(NodeNetworkStateId::SLEEP) {
    this->player = player;
}

NodeSleep::~NodeSleep() {
    player = nullptr;
}

void NodeSleep::onStateMounted(PDN* pdn) {
    pdn->getDisplay()->
        invalidateScreen()->
        drawImage(getNodeImage(ImageType::LOGO_LEFT))->
        drawImage(getNodeImage(ImageType::LOGO_RIGHT))->
        render();

    transitionToAwakenSequenceState = false;
    dormantTimer.setTimer(SLEEP_DURATION);
}

void NodeSleep::onStateLoop(PDN* pdn) {
    dormantTimer.updateTime();
    if (dormantTimer.expired()) {
        transitionToAwakenSequenceState = true;
    }
}

void NodeSleep::onStateDismounted(PDN* pdn) {
    dormantTimer.invalidate();
}

bool NodeSleep::transitionToAwakenSequence() {
    return transitionToAwakenSequenceState;
}
