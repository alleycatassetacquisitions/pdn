#include "apps/symbol-match/symbol-match-states.hpp"
#include "device/animation/animation-base.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/drivers/logger.hpp"
#include "apps/symbol-match/../../device/animation/fdn-match-success-animation.hpp"

static const char* TAG = "MatchSuccess";

MatchSuccess::MatchSuccess(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                           SymbolWirelessManager* symbolWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, MATCH_SUCCESS)
    , symbolWirelessManager(symbolWirelessManager) {}

MatchSuccess::~MatchSuccess() { symbolWirelessManager = nullptr; }

void MatchSuccess::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    codewordTimer.setTimer(CODEWORD_DURATION_MS);

    AnimationConfig cfg{};
    cfg.loop  = true;
    cfg.speed = 20;
    fdn->getLightManager()->startAnimation(new FDNMatchSuccessAnimation(), cfg);

    Display* d = fdn->getDisplay();
    d->invalidateScreen();
    d->setGlyphMode(FontMode::TEXT);
    d->drawCenteredText(FDN_CODEWORD, 38);
    d->render();

    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SYMBOL_MATCH_SUCCESS, SymbolId::SYMBOL_A, port);
        }
    }
}

void MatchSuccess::onStateLoop(FDN* fdn) {
    if (codewordTimer.expired()) {
        transitionToSelectionState = true;
    }
}

void MatchSuccess::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    codewordTimer.invalidate();
    transitionToSelectionState = false;
    fdn->getLightManager()->stopAnimation();
}

bool MatchSuccess::transitionToSelection() { return transitionToSelectionState; }
