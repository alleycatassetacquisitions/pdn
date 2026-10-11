#include "game/node-network-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

#define NODE_DUEL_RESULT_TAG "NODE_DUEL_RESULT"

NodeDuelResult::NodeDuelResult(Player* player, NodeMatchManager* matchManager)
    : TypedState<PDN>(NodeNetworkStateId::DUEL_RESULT) {
    this->player = player;
    this->matchManager = matchManager;
}

NodeDuelResult::~NodeDuelResult() {
    player = nullptr;
    matchManager = nullptr;
}

void NodeDuelResult::onStateMounted(PDN* pdn) {
    matchManager->recordLinkOutcome();

    if (matchManager->didSync()) {
        syncSuccess = true;
        player->incrementMatchesPlayed();
        matchManager->finalizeMatch();
    } else {
        linkFailed = true;
        matchManager->clearCurrentMatch();
    }

    pdn->getHaptics()->setIntensity(0);
    pdn->getDisplay()->invalidateScreen()->render();
}

void NodeDuelResult::onStateLoop(PDN* pdn) {
}

void NodeDuelResult::onStateDismounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    syncSuccess = false;
    linkFailed = false;
    pdn->getLightManager()->stopAnimation();
}

bool NodeDuelResult::transitionToWin() {
    return syncSuccess;
}

bool NodeDuelResult::transitionToLinkRetry() {
    return linkFailed;
}
