#include "game/node-network/node-network-states.hpp"

#include <string>

#include "device/animation/vertical-chase-animation.hpp"
#include "device/drivers/light-interface.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/node-network/node-network-resources.hpp"
#include "symbol-match/symbol-manager.hpp"
#include "wireless/mac-functions.hpp"

#define TAG "NodeIdle"
#include "device/drivers/logger.hpp"

NodeIdle::NodeIdle(Player* player, MatchManagerV2* matchManager, NodeNetworkStats* stats,
                   RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, NODE_IDLE)
    , player(player)
    , matchManager(matchManager)
    , stats(stats) {}

NodeIdle::~NodeIdle() {
    player = nullptr;
    matchManager = nullptr;
    stats = nullptr;
}

void NodeIdle::onStateMounted(PDN* pdn) {
    pdn->getWirelessManager()->enablePeerCommsMode();
    statsIndex = 0;
    displayIsDirty = true;
    transitionToSymbolState = false;
    offerTimer.invalidate();
    lastLinkView = LinkView::UNSET;

    parameterizedCallbackFunction cycleStats = [](void* ctx) {
        NodeIdle* idle = static_cast<NodeIdle*>(ctx);
        idle->statsIndex = (idle->statsIndex + 1) % 2;
        idle->displayIsDirty = true;
    };
    pdn->getPrimaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);

    AnimationConfig config;
    config.speed = 5;
    config.curve = EaseCurve::ELASTIC;
    config.initialState = NODE_NETWORK_IDLE_LED_STATE;
    config.loopDelayMs = 1500;
    config.loop = true;
    pdn->getLightManager()->startAnimation(new VerticalChaseAnimation(), config);
}

bool NodeIdle::isPdnLinkedOn(SerialIdentifier jack) {
    return getJackStatus(jack) == PortStatus::CONNECTED &&
           getPeerDeviceType(jack) == DeviceType::PDN &&
           remoteDeviceCoordinator->getPeerMac(jack) != nullptr;
}

void NodeIdle::logLinkViewIfChanged(LinkView view) {
    if (view == lastLinkView) {
        return;
    }
    lastLinkView = view;

    switch (view) {
        case LinkView::UNSET:
            break;
        case LinkView::NONE:
            LOG_I(TAG, "link: no PDN linked");
            break;
        case LinkView::INITIATOR:
            LOG_I(TAG, "link: PDN on output jack -> initiator, peer %s",
                  MacToString(remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK)));
            break;
        case LinkView::RESPONDER:
            LOG_I(TAG, "link: PDN on input jack -> responder, peer %s",
                  MacToString(remoteDeviceCoordinator->getPeerMac(SerialIdentifier::INPUT_JACK)));
            break;
        case LinkView::IGNORED:
            LOG_W(TAG, "link: ignored (daisy-chained or PDN on both jacks)");
            break;
    }
}

void NodeIdle::updateLinkSideAndMatchStart() {
    if (getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::FDN ||
        getPeerDeviceType(SerialIdentifier::INPUT_JACK) == DeviceType::FDN) {
        if (!transitionToSymbolState) {
            LOG_I(TAG, "FDN on jack -> symbol flow");
        }
        transitionToSymbolState = true;
    }

    const bool outputLinked = isPdnLinkedOn(SerialIdentifier::OUTPUT_JACK);
    const bool inputLinked = isPdnLinkedOn(SerialIdentifier::INPUT_JACK);
    // Daisy-chaining is unsupported here: a chained jack, or a PDN on both jacks, is ignored.
    const bool chained = getJackStatus(SerialIdentifier::OUTPUT_JACK) == PortStatus::DAISY_CHAINED ||
                         getJackStatus(SerialIdentifier::INPUT_JACK) == PortStatus::DAISY_CHAINED;

    if (chained || outputLinked == inputLinked) {
        logLinkViewIfChanged(chained || outputLinked ? LinkView::IGNORED : LinkView::NONE);
        offerTimer.invalidate();
        if (!matchManager->isHandshakeComplete()) {
            matchManager->clearCurrentMatch();
            matchManager->clearLocalLinkSide();
        }
        return;
    }

    if (inputLinked) {
        logLinkViewIfChanged(LinkView::RESPONDER);
        matchManager->setLocalLinkSide(DuelLinkSide::RESPONDER);
        return;
    }

    logLinkViewIfChanged(LinkView::INITIATOR);
    matchManager->setLocalLinkSide(DuelLinkSide::INITIATOR);

    // An OFFER sent before the peer finished its own serial handshake is dropped, so re-offer.
    if (offerTimer.expired()) {
        LOG_W(TAG, "OFFER unanswered after %lu ms: re-offering",
              static_cast<unsigned long>(MATCH_OFFER_TIMEOUT_MS));
        matchManager->clearCurrentMatch();
        offerTimer.invalidate();
    }
    if (!offerTimer.isRunning()) {
        matchManager->startMatch(remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK));
        offerTimer.setTimer(MATCH_OFFER_TIMEOUT_MS);
    }
}

void NodeIdle::onStateLoop(PDN* pdn) {
    if (displayIsDirty) {
        renderStats(pdn);
        displayIsDirty = false;
    }

    updateLinkSideAndMatchStart();
}

void NodeIdle::onStateDismounted(PDN* pdn) {
    statsIndex = 0;
    pdn->getLightManager()->stopAnimation();
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    transitionToSymbolState = false;
    offerTimer.invalidate();
}

bool NodeIdle::transitionToDuelCountdown() {
    return matchManager->isHandshakeComplete();
}

bool NodeIdle::transitionToSymbol() {
    return transitionToSymbolState;
}

bool NodeIdle::isPrimaryRequired() {
    return true;
}

bool NodeIdle::isAuxRequired() {
    return true;
}

void NodeIdle::renderStats(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen();
    pdn->getDisplay()->drawImage(nodeNetworkImage(ImageType::IDLE))->render();

    if (statsIndex == 0) {
        pdn->getDisplay()
            ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
            ->drawText("Nodes", 77, 20)
            ->drawText("Created", 72, 35);
        const std::string countText = std::to_string(stats->getNodesCreated());
        pdn->getDisplay()
            ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
            ->drawText(countText.c_str(), 80, 55);
    } else {
        const int glyphSize = 32;
        if (player->getSymbol() != nullptr) {
            pdn->getDisplay()
                ->setGlyphMode(FontMode::SYMBOL_GLYPH)
                ->renderGlyph(player->getSymbol()->getSymbolGlyph(),
                              static_cast<int>(64 + (64 - glyphSize) / 2),
                              static_cast<int>(64 - (64 - glyphSize) / 2));
        }
    }

    pdn->getDisplay()->render();
}
