#include "game/node-network-states.hpp"
#include "game/node-network-resources.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/device.hpp"
#include <cstdio>

NodeWin::NodeWin(Player* player, NodeMatchManager* matchManager)
    : TypedState<PDN>(NodeNetworkStateId::WIN) {
    this->player = player;
    this->matchManager = matchManager;
}

NodeWin::~NodeWin() {
    player = nullptr;
    matchManager = nullptr;
}

void NodeWin::onStateMounted(PDN* pdn) {
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    pdn->getDisplay()->invalidateScreen()->drawImage(getNodeImage(ImageType::WIN));

    const NodeLastMatchDisplay& display = matchManager->getLastMatchDisplay();
    if (display.hasData) {
        pdn->getDisplay()->drawImage(Image(image_stats_card, 60, 64, 0, 0));
        char myLine[16];
        char oppLine[16];
        snprintf(myLine, sizeof(myLine), "You:%4lu", display.myTimeMs);
        snprintf(oppLine, sizeof(oppLine), "Opp:%4lu", display.opponentTimeMs);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText(myLine, 2, 12);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText(oppLine, 2, 44);
    }

    pdn->getDisplay()->render();
    winTimer.setTimer(8000);

    AnimationConfig config;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;
    pdn->getLightManager()->startAnimation(new HunterWinAnimation(), config);
}

void NodeWin::onStateLoop(PDN* pdn) {
    winTimer.updateTime();
    if (winTimer.expired()) {
        reset = true;
    }
}

void NodeWin::onStateDismounted(PDN* pdn) {
    winTimer.invalidate();
    reset = false;
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
}

bool NodeWin::resetGame() {
    return reset;
}

bool NodeWin::isTerminalState() {
    return true;
}
