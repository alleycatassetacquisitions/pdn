#include "apps/symbol-lock/symbol-lock-states.hpp"
#include "device/animation/animation-base.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/animation/fdn-match-success-animation.hpp"

static const char* TAG = "SymbolLockMatchSuccess";

SymbolLockMatchSuccessState::SymbolLockMatchSuccessState(
    SymbolLockManager* symbolManager,
    SymbolWirelessManager* symbolWirelessManager,
    RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : TypedState<FDN>(SymbolLockStateId::SYMBOL_LOCK_MATCH_SUCCESS)
    , symbolManager(symbolManager)
    , symbolWirelessManager(symbolWirelessManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator) {}

SymbolLockMatchSuccessState::~SymbolLockMatchSuccessState() {
    symbolManager = nullptr;
    symbolWirelessManager = nullptr;
    remoteDeviceCoordinator = nullptr;
}

void SymbolLockMatchSuccessState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    demoTransitionReady = false;
    toggleBlink = true;
    bufferTimer.setTimer(bufferInterval);
    renderTimer.setTimer(renderInterval);
    renderSymbolScreen(fdn);

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

void SymbolLockMatchSuccessState::onStateLoop(FDN* fdn) {
    if (renderTimer.expired()) {
        renderSymbolScreen(fdn);
        renderTimer.setTimer(renderInterval);
    }

    if (bufferTimer.expired() && !demoTransitionReady) {
        demoTransitionReady = true;
    }
}

void SymbolLockMatchSuccessState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    bufferTimer.invalidate();
    renderTimer.invalidate();
    toggleBlink = true;
    demoTransitionReady = false;
    fdn->getLightManager()->stopAnimation();
}

void SymbolLockMatchSuccessState::renderSymbolScreen(FDN* fdn) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen();
    d->whiteScreen();
    d->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (symbolManager->isSingleSymbolMode()) {
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK), 48, 40);
    } else {
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK), 24, 40);
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK_SECONDARY), 72, 40);
    }
    d->render();
}

bool SymbolLockMatchSuccessState::transitionToDemoModule() {
    return demoTransitionReady;
}
