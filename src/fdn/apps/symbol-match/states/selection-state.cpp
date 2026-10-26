#include "apps/symbol-match/symbol-match-states.hpp"
#include "device/animation/animation-base.hpp"
#include "device/animation/fdn-idle-animation.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "Selection";

namespace {
constexpr uint8_t IDLE_ANIMATION_SPEED = 20;
}

Selection::Selection(SymbolManager* symbolManager,
                     RemoteDeviceCoordinator* remoteDeviceCoordinator,
                     SymbolWirelessManager* symbolWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, SELECTION)
    , symbolManager(symbolManager)
    , symbolWirelessManager(symbolWirelessManager) {}

Selection::~Selection() {
    symbolManager = nullptr;
    symbolWirelessManager = nullptr;
}

void Selection::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    symbolManager->getRefreshTimer()->invalidate();
    symbolManager->refreshSymbols();
    bufferTimer.setTimer(bufferInterval);

    AnimationConfig cfg{};
    cfg.loop = true;
    cfg.speed = IDLE_ANIMATION_SPEED;
    fdn->getLightManager()->startAnimation(new FDNIdleAnimation(), cfg);

    // Unacked: a PDN that misses this keeps last round's state (win screen, or a stale
    // matchReady) until the next refresh or an unplug.
    for (SerialIdentifier port :
         {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SYMBOLS_REFRESHED, SymbolId::SYMBOL_A,
                                              port);
        }
    }
}

void Selection::onStateLoop(FDN* fdn) {
    if (bufferTimer.expired()) {
        transitionToIdleState = true;
    } else if (bufferTimer.isRunning()) {
        showLoadingGlyphs(fdn->getDisplay());
    }
}

void Selection::onStateDismounted(FDN* fdn) {
    bufferTimer.invalidate();
    transitionToIdleState = false;
    // SymbolIdle shows only its per-jack match lights; a running animation would repaint them.
    fdn->getLightManager()->stopAnimation();
    fdn->getDisplay()->invalidateScreen();
    symbolManager->resetRefreshTimer();
    LOG_W(TAG, "Dismounted");
}

bool Selection::transitionToIdle() {
    return transitionToIdleState;
}
