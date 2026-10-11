#include "game/node-network-states.hpp"
#include "game/node-network-resources.hpp"
#include "device/animation/lose-animation.hpp"
#include "device/device.hpp"

NodeLinkRetry::NodeLinkRetry(Player* player, NodeMatchManager* matchManager)
    : TypedState<PDN>(NodeNetworkStateId::LINK_RETRY) {
    this->player = player;
    this->matchManager = matchManager;
}

NodeLinkRetry::~NodeLinkRetry() {
    player = nullptr;
    matchManager = nullptr;
}

void NodeLinkRetry::onStateMounted(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen()->drawImage(getNodeImage(ImageType::CONNECT));
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Out of", 8, 48);
    pdn->getDisplay()->drawText("sync", 8, 60);
    pdn->getDisplay()->render();

    retryTimer.setTimer(4000);

    AnimationConfig config;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;
    pdn->getLightManager()->startAnimation(new LoseAnimation(), config);
}

void NodeLinkRetry::onStateLoop(PDN* pdn) {
    retryTimer.updateTime();
    if (retryTimer.expired()) {
        goIdle = true;
    }
}

void NodeLinkRetry::onStateDismounted(PDN* pdn) {
    retryTimer.invalidate();
    goIdle = false;
    pdn->getLightManager()->stopAnimation();
}

bool NodeLinkRetry::transitionToIdle() {
    return goIdle;
}
