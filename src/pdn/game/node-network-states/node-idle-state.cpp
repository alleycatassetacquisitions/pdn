#include "device/drivers/serial-wrapper.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/node-network-states.hpp"
#include "game/node-network-resources.hpp"
#include "device/device.hpp"
#include "device/animation/idle-animation.hpp"
#include "state/connect-state.hpp"

NodeIdle::NodeIdle(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, NodeNetworkStateId::IDLE) {
    this->player = player;
    this->matchManager = matchManager;
}

NodeIdle::~NodeIdle() {
    player = nullptr;
    matchManager = nullptr;
}

void NodeIdle::onStateMounted(PDN* pdn) {
    pdn->getWirelessManager()->enablePeerCommsMode();

    AnimationConfig config;
    AnimationBase* animation = new IdleAnimation();
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = HUNTER_IDLE_STATE_ALTERNATE;
    config.loopDelayMs = 0;
    config.loop = true;
    pdn->getLightManager()->startAnimation(animation, config);

    parameterizedCallbackFunction cycleStats = [](void* ctx) {
        NodeIdle* idle = static_cast<NodeIdle*>(ctx);
        idle->statsIndex++;
        if (idle->statsIndex > idle->statsCount) {
            idle->statsIndex = 0;
        }
        idle->displayIsDirty = true;
    };

    pdn->getPrimaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);
    displayIsDirty = true;
}

void NodeIdle::onStateLoop(PDN* pdn) {
    if (displayIsDirty) {
        renderStats(pdn);
        displayIsDirty = false;
    }

    // FDN symbol match stays on Quickdraw; event PDN↔PDN build ignores FDN jacks here.
    if (isConnected()
        && getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::PDN) {
        if (!matchInitialized) {
            const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
            if (peerMac != nullptr) {
                matchManager->initializeMatch(const_cast<uint8_t*>(peerMac));
                matchInitialized = true;
                matchInitializationTimer.setTimer(MATCH_INITIALIZATION_TIMEOUT);
            }
        }
    }

    if (matchInitializationTimer.expired()) {
        matchInitialized = false;
        matchManager->clearCurrentMatch();
    }
}

void NodeIdle::onStateDismounted(PDN* pdn) {
    statsIndex = 0;
    matchInitializationTimer.invalidate();
    matchInitialized = false;
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
}

bool NodeIdle::transitionToDuelCountdown() {
    return matchManager->isMatchReady();
}

void NodeIdle::renderStats(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen();
    pdn->getDisplay()->drawImage(getNodeImage(ImageType::IDLE))->render();

    if (statsIndex == 0) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Links", 74, 20);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getMatchesPlayed()).c_str(), 88, 40);
    } else if (statsIndex == 1) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Last", 70, 20)->drawText("Reaction", 70, 35);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getLastReactionTime()).c_str(), 80, 55);
    } else if (statsIndex == 2) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Average", 70, 20)->drawText("Reaction", 70, 35);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(player->getAverageReactionTime()).c_str(), 80, 55);
    } else if (statsIndex == 3) {
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText("Retry", 74, 20);
        pdn->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)->drawText(std::to_string(matchManager->getFailedAttempts()).c_str(), 88, 40);
    }

    pdn->getDisplay()->render();
}

bool NodeIdle::isPrimaryRequired() {
    return getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::PDN;
}

bool NodeIdle::isAuxRequired() {
    return getPeerDeviceType(SerialIdentifier::INPUT_JACK) == DeviceType::PDN;
}
