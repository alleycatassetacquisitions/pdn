#include "game/node-network/node-network-states.hpp"

#include "device/device.hpp"

NodeIdle::NodeIdle()
    : TypedState<PDN>(NODE_IDLE) {
}

NodeIdle::~NodeIdle() = default;

void NodeIdle::onStateMounted(PDN* pdn) {
    pdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT)
        ->drawText("Node Network", 0, 24)
        ->drawText("Connect", 0, 44)
        ->render();
}

void NodeIdle::onStateLoop(PDN* pdn) {
}

void NodeIdle::onStateDismounted(PDN* pdn) {
}
