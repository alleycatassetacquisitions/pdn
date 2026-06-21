#include "apps/symbol-lock/symbol-lock-states.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/drivers/light-interface.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolLockIdle";

namespace {
constexpr uint8_t kNumFin    = 9;
constexpr uint8_t kNumRecess = 22;

void applyMatchSideHalves(FDNLightManager* lm, bool leftOn, bool rightOn) {
    constexpr LEDColor kColor(100, 160, 255);
    constexpr uint8_t kBright = 90;

    lm->clearAllLights(kNumRecess, kNumFin);

    if (leftOn) {
        for (uint8_t i = 0; i < 11; ++i) {
            lm->setRecessLight((i + 2) % kNumRecess, kColor, kBright);
        }
        for (uint8_t i = 4; i < kNumFin; ++i) {
            lm->setFinLight(i, kColor, kBright);
        }
    }
    if (rightOn) {
        for (uint8_t i = 11; i < 22; ++i) {
            lm->setRecessLight((i + 2) % kNumRecess, kColor, kBright);
        }
        for (uint8_t i = 0; i < 4; ++i) {
            lm->setFinLight(i, kColor, kBright);
        }
    }
}

bool isPortConnected(RemoteDeviceCoordinator* rdc, SerialIdentifier port) {
    const PortStatus status = rdc->getPortStatus(port);
    return status == PortStatus::CONNECTED || status == PortStatus::DAISY_CHAINED;
}
}  // namespace

void SymbolLockIdleState::refreshConnectionState() {
    leftConnected  = isPortConnected(remoteDeviceCoordinator, SerialIdentifier::INPUT_JACK);
    rightConnected = isPortConnected(remoteDeviceCoordinator, SerialIdentifier::INPUT_JACK_SECONDARY);
}

void SymbolLockIdleState::sendSymbolToPort(SerialIdentifier port, bool& sentFlag, SimpleTimer& resendTimer) {
    const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
    if (peerMac == nullptr) {
        return;
    }

    symbolWirelessManager->setMacPeer(peerMac);
    symbolWirelessManager->sendPacket(
        SMCommand::SEND_SYMBOL,
        symbolManager->getSymbol(port)->getSymbolId(),
        port);
    sentFlag = true;
    resendTimer.setTimer(kSymbolResendIntervalMs);
}

SymbolLockIdleState::SymbolLockIdleState(
    SymbolLockManager* symbolManager,
    RemoteDeviceCoordinator* remoteDeviceCoordinator,
    SymbolWirelessManager* symbolWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, SymbolLockStateId::SYMBOL_LOCK_IDLE)
    , symbolManager(symbolManager)
    , symbolWirelessManager(symbolWirelessManager) {}

SymbolLockIdleState::~SymbolLockIdleState() {
    symbolWirelessManager = nullptr;
}

void SymbolLockIdleState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    mountedFdn = fdn;
    renderSymbolScreen(fdn);

    symbolWirelessManager->setPacketReceivedCallback(
        [this](const SymbolMatchCommand& command) {
            if (command.command != SMCommand::SEND_SYMBOL) return;
            refreshConnectionState();
            const SymbolId local = symbolManager->getSymbol(SerialIdentifier::INPUT_JACK)->getSymbolId();
            symbolManager->setMatched(SerialIdentifier::INPUT_JACK, command.symbolId == local);
            symbolSentLeft = true;
            if (mountedFdn) { renderSymbolScreen(mountedFdn); syncMatchSideLights(mountedFdn); }
        }, SerialIdentifier::INPUT_JACK);

    symbolWirelessManager->setPacketReceivedCallback(
        [this](const SymbolMatchCommand& command) {
            if (command.command != SMCommand::SEND_SYMBOL) return;
            refreshConnectionState();
            const SymbolId local = symbolManager->getSymbol(SerialIdentifier::INPUT_JACK_SECONDARY)->getSymbolId();
            symbolManager->setMatched(SerialIdentifier::INPUT_JACK_SECONDARY, command.symbolId == local);
            symbolSentRight = true;
            if (mountedFdn) { renderSymbolScreen(mountedFdn); syncMatchSideLights(mountedFdn); }
        }, SerialIdentifier::INPUT_JACK_SECONDARY);

    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac) {
            if (port == SerialIdentifier::INPUT_JACK) {
                sendSymbolToPort(port, symbolSentLeft, symbolResendTimerLeft);
            } else {
                sendSymbolToPort(port, symbolSentRight, symbolResendTimerRight);
            }
        }
    }

    refreshConnectionState();
    bool leftLeds  = symbolManager->isMatched(SerialIdentifier::INPUT_JACK) && symbolSentLeft;
    bool rightLeds = symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY) && symbolSentRight;
    lastSideLightLeft_  = leftLeds;
    lastSideLightRight_ = rightLeds;
    updateMatchSideLights(fdn, leftLeds, rightLeds);
}

void SymbolLockIdleState::onStateLoop(FDN* fdn) {
    if (symbolManager->getRefreshTimer()->expired()) {
        transitionToSelectionState = true;
        return;
    }

    refreshConnectionState();

    if (!leftConnected) {
        symbolManager->setMatched(SerialIdentifier::INPUT_JACK, false);
        if (mountedFdn) { renderSymbolScreen(mountedFdn); syncMatchSideLights(mountedFdn); }
    }
    if (!rightConnected) {
        symbolManager->setMatched(SerialIdentifier::INPUT_JACK_SECONDARY, false);
        if (mountedFdn) { renderSymbolScreen(mountedFdn); syncMatchSideLights(mountedFdn); }
    }

    if (!symbolSentLeft && leftConnected) {
        sendSymbolToPort(SerialIdentifier::INPUT_JACK, symbolSentLeft, symbolResendTimerLeft);
    } else if (!leftConnected) {
        symbolSentLeft = false;
        symbolResendTimerLeft.invalidate();
    } else if (leftConnected
               && !symbolManager->isMatched(SerialIdentifier::INPUT_JACK)
               && symbolResendTimerLeft.expired()) {
        sendSymbolToPort(SerialIdentifier::INPUT_JACK, symbolSentLeft, symbolResendTimerLeft);
    }

    if (!symbolSentRight && rightConnected) {
        sendSymbolToPort(SerialIdentifier::INPUT_JACK_SECONDARY, symbolSentRight, symbolResendTimerRight);
    } else if (!rightConnected) {
        symbolSentRight = false;
        symbolResendTimerRight.invalidate();
    } else if (rightConnected
               && !symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY)
               && symbolResendTimerRight.expired()) {
        sendSymbolToPort(SerialIdentifier::INPUT_JACK_SECONDARY, symbolSentRight, symbolResendTimerRight);
    }

    if (symbolManager->getRefreshTimer()->isRunning()) {
        int elapsed = symbolManager->getRefreshTimer()->getElapsedTime();
        if (elapsed - lastTimeRendered >= 500) {
            lastTimeRendered = elapsed;
            renderSymbolScreen(fdn);
            blinkToggle = !blinkToggle;
        }
    }

    syncMatchSideLights(fdn);
}

void SymbolLockIdleState::onStateDismounted(FDN* fdn) {
    transitionToSelectionState = false;
    mountedFdn                 = nullptr;
    lastTimeRendered           = 0;
    symbolSentLeft             = false;
    symbolSentRight            = false;
    symbolResendTimerLeft.invalidate();
    symbolResendTimerRight.invalidate();

    symbolWirelessManager->clearCallback();
    symbolManager->setMatched(SerialIdentifier::INPUT_JACK, false);
    symbolManager->setMatched(SerialIdentifier::INPUT_JACK_SECONDARY, false);

    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SYMBOLS_REFRESHED, SymbolId::SYMBOL_A, port);
        }
    }

    fdn->getLightManager()->stopAnimation();
    LOG_W(TAG, "Dismounted");
}

void SymbolLockIdleState::updateMatchSideLights(FDN* fdn, bool leftOn, bool rightOn) {
    auto* lm = static_cast<FDNLightManager*>(fdn->getLightManager());
    if (symbolManager->isSingleSymbolMode()) {
        const bool matched = leftOn || rightOn;
        applyMatchSideHalves(lm, matched, matched);
        return;
    }
    applyMatchSideHalves(lm, leftOn, rightOn);
}

void SymbolLockIdleState::syncMatchSideLights(FDN* fdn) {
    const bool l = symbolManager->isMatched(SerialIdentifier::INPUT_JACK) && symbolSentLeft;
    const bool r = symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY) && symbolSentRight;
    if (l != lastSideLightLeft_ || r != lastSideLightRight_) {
        lastSideLightLeft_  = l;
        lastSideLightRight_ = r;
        updateMatchSideLights(fdn, l, r);
    }
}

void SymbolLockIdleState::renderSymbolScreen(FDN* fdn) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen();

    if (symbolManager->isSingleSymbolMode()) {
        const bool anyConnected = leftConnected || rightConnected;
        const bool allMatched =
            symbolManager->allConnectedPortsMatched(leftConnected, rightConnected);

        if (allMatched) {
            d->whiteScreen();
        }

        d->setGlyphMode(FontMode::SYMBOL_GLYPH);
        // Steady when idle (no PDN) or fully matched; blink only while waiting on a connected PDN.
        if (allMatched || !anyConnected || blinkToggle) {
            d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK), 48, 40);
        }
    } else {
        if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK))           d->whiteScreenLeftHalf();
        if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY)) d->whiteScreenRightHalf();

        d->setGlyphMode(FontMode::SYMBOL_GLYPH);

        if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK) || !leftConnected || blinkToggle)
            d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK), 24, 40);

        if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY) || !rightConnected || blinkToggle)
            d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK_SECONDARY), 72, 40);
    }

    d->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);

    int timeLeft = symbolManager->getTimeLeftToRefresh();
    int minutes  = timeLeft / 60000;
    int seconds  = (timeLeft % 60000) / 1000;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    d->drawText(buf, 40, 64);

    d->render();
}

bool SymbolLockIdleState::symbolsSentToConnectedPorts() const {
    if (leftConnected && !symbolSentLeft) {
        return false;
    }
    if (rightConnected && !symbolSentRight) {
        return false;
    }
    return leftConnected || rightConnected;
}

bool SymbolLockIdleState::transitionToSelection() {
    return transitionToSelectionState;
}

bool SymbolLockIdleState::transitionToMatchSuccess() {
    if (symbolManager->isSingleSymbolMode()) {
        return symbolManager->allConnectedPortsMatched(leftConnected, rightConnected)
            && symbolsSentToConnectedPorts();
    }

    return leftConnected
        && rightConnected
        && symbolManager->isMatched(SerialIdentifier::INPUT_JACK)
        && symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY)
        && symbolSentLeft
        && symbolSentRight;
}
