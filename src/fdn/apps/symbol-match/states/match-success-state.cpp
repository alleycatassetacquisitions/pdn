#include "apps/symbol-match/symbol-match-states.hpp"
#include "device/animation/animation-base.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/drivers/logger.hpp"
#include "apps/symbol-match/../../device/animation/fdn-match-success-animation.hpp"

static const char* TAG = "MatchSuccess";

MatchSuccess::MatchSuccess(SymbolManager* symbolManager,
                           RemoteDeviceCoordinator* remoteDeviceCoordinator,
                           SymbolWirelessManager* symbolWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, MATCH_SUCCESS)
    , symbolManager(symbolManager)
    , symbolWirelessManager(symbolWirelessManager) {}

MatchSuccess::~MatchSuccess() {
    symbolManager = nullptr;
    symbolWirelessManager = nullptr;
}

void MatchSuccess::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    bufferTimer.setTimer(bufferInterval);
    renderTimer.setTimer(renderInterval);

    AnimationConfig cfg{};
    cfg.loop  = true;
    cfg.speed = 20;
    fdn->getLightManager()->startAnimation(new FDNMatchSuccessAnimation(), cfg);

    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SYMBOL_MATCH_SUCCESS, SymbolId::SYMBOL_A, port);
        }
    }
}

void MatchSuccess::onStateLoop(FDN* fdn) {
    if (renderTimer.expired()) {
        renderSymbolScreen(fdn);
        renderTimer.setTimer(renderInterval);
    }
}

void MatchSuccess::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    bufferTimer.invalidate();
    renderTimer.invalidate();
    toggleBlink = true;
    fdn->getLightManager()->stopAnimation();
}

void MatchSuccess::renderSymbolScreen(FDN* fdn) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen();
    d->whiteScreen();
    d->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (toggleBlink) {
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK), 24, 40);
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK_SECONDARY), 72, 40);
    }
    d->render();
    toggleBlink = !toggleBlink;
}

bool MatchSuccess::transitionToMainMenu() {
    return bufferTimer.expired();
}
