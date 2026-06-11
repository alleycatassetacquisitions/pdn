#include <cstring>
#include "apps/symbol-match/symbol-match-states.hpp"
#include "device/fdn-light-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/light-interface.hpp"
#include "device/drivers/logger.hpp"
#include "symbol.hpp"

static const char* TAG = "SymbolIdle";

namespace {
constexpr uint8_t kNumFin    = 9;
constexpr uint8_t kNumRecess = 22;

// Build a light state for the half-screen match indicators.
// leftOn  → illuminate left  half of recess + left-side fin lights
// rightOn → illuminate right half of recess + right-side fin lights
void applyMatchSideHalves(FDNLightManager* lm, bool leftOn, bool rightOn) {
    constexpr LEDColor kColor(100, 160, 255);
    constexpr uint8_t kBright = 90;

    // Clear all
    lm->clearAllLights(kNumRecess, kNumFin);

    if (leftOn) {
        // Left recess half: indices 0-10
        for (uint8_t i = 0; i < 11; ++i) {
            lm->setRecessLight((i + 2) % kNumRecess, kColor, kBright);
        }
        // Left fin lights: indices 4-8
        for (uint8_t i = 4; i < kNumFin; ++i) {
            lm->setFinLight(i, kColor, kBright);
        }
    }
    if (rightOn) {
        // Right recess half: indices 11-21
        for (uint8_t i = 11; i < 22; ++i) {
            lm->setRecessLight((i + 2) % kNumRecess, kColor, kBright);
        }
        // Right fin lights: indices 0-3
        for (uint8_t i = 0; i < 4; ++i) {
            lm->setFinLight(i, kColor, kBright);
        }
    }
}
}  // namespace

SymbolIdle::SymbolIdle(SymbolManager* symbolManager,
                       RemoteDeviceCoordinator* remoteDeviceCoordinator,
                       SymbolWirelessManager* symbolWirelessManager)
    : FDNConnectState(remoteDeviceCoordinator, SYMBOL_IDLE)
    , symbolManager(symbolManager)
    , symbolWirelessManager(symbolWirelessManager) {}

SymbolIdle::~SymbolIdle() {
    symbolWirelessManager = nullptr;
}

void SymbolIdle::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    mountedFdn = fdn;
    renderSymbolScreen(fdn);

    symbolWirelessManager->setPacketReceivedCallback(
        [this](const SymbolMatchCommand& command) {
            if (command.command != SMCommand::SEND_SYMBOL) return;
            const SymbolId local = symbolManager->getSymbol(SerialIdentifier::INPUT_JACK)->getSymbolId();
            symbolManager->setMatched(SerialIdentifier::INPUT_JACK, command.symbolId == local);
            if (mountedFdn) { renderSymbolScreen(mountedFdn); syncMatchSideLights(mountedFdn); }
        }, SerialIdentifier::INPUT_JACK);

    symbolWirelessManager->setPacketReceivedCallback(
        [this](const SymbolMatchCommand& command) {
            if (command.command != SMCommand::SEND_SYMBOL) return;
            const SymbolId local = symbolManager->getSymbol(SerialIdentifier::INPUT_JACK_SECONDARY)->getSymbolId();
            symbolManager->setMatched(SerialIdentifier::INPUT_JACK_SECONDARY, command.symbolId == local);
            if (mountedFdn) { renderSymbolScreen(mountedFdn); syncMatchSideLights(mountedFdn); }
        }, SerialIdentifier::INPUT_JACK_SECONDARY);

    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(
                SMCommand::SEND_SYMBOL,
                symbolManager->getSymbol(port)->getSymbolId(),
                port);
        }
    }

    leftConnected  = remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED;
    rightConnected = remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK_SECONDARY) == PortStatus::CONNECTED;
    bool leftLeds  = symbolManager->isMatched(SerialIdentifier::INPUT_JACK) && symbolSentLeft;
    bool rightLeds = symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY) && symbolSentRight;
    lastSideLightLeft_  = leftLeds;
    lastSideLightRight_ = rightLeds;
    updateMatchSideLights(fdn, leftLeds, rightLeds);
}

void SymbolIdle::onStateLoop(FDN* fdn) {
    if (symbolManager->getRefreshTimer()->expired()) {
        transitionToSelectionState = true;
        return;
    }

    leftConnected  = remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED;
    rightConnected = remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK_SECONDARY) == PortStatus::CONNECTED;

    if (!leftConnected) {
        symbolManager->setMatched(SerialIdentifier::INPUT_JACK, false);
        if (mountedFdn) { renderSymbolScreen(mountedFdn); syncMatchSideLights(mountedFdn); }
    }
    if (!rightConnected) {
        symbolManager->setMatched(SerialIdentifier::INPUT_JACK_SECONDARY, false);
        if (mountedFdn) { renderSymbolScreen(mountedFdn); syncMatchSideLights(mountedFdn); }
    }

    if (!symbolSentLeft && leftConnected) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::INPUT_JACK);
        if (peerMac) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SEND_SYMBOL,
                symbolManager->getSymbol(SerialIdentifier::INPUT_JACK)->getSymbolId(),
                SerialIdentifier::INPUT_JACK);
            symbolSentLeft = true;
        }
    } else if (!leftConnected) {
        symbolSentLeft = false;
    }

    if (!symbolSentRight && rightConnected) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::INPUT_JACK_SECONDARY);
        if (peerMac) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SEND_SYMBOL,
                symbolManager->getSymbol(SerialIdentifier::INPUT_JACK_SECONDARY)->getSymbolId(),
                SerialIdentifier::INPUT_JACK_SECONDARY);
            symbolSentRight = true;
        }
    } else if (!rightConnected) {
        symbolSentRight = false;
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

void SymbolIdle::onStateDismounted(FDN* fdn) {
    transitionToSelectionState = false;
    transitionToMainMenuApp    = false;
    mountedFdn                 = nullptr;
    lastTimeRendered           = 0;
    symbolSentLeft             = false;
    symbolSentRight            = false;

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

void SymbolIdle::updateMatchSideLights(FDN* fdn, bool leftOn, bool rightOn) {
    auto* lm = static_cast<FDNLightManager*>(fdn->getLightManager());
    applyMatchSideHalves(lm, leftOn, rightOn);
}

void SymbolIdle::syncMatchSideLights(FDN* fdn) {
    const bool l = symbolManager->isMatched(SerialIdentifier::INPUT_JACK) && symbolSentLeft;
    const bool r = symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY) && symbolSentRight;
    if (l != lastSideLightLeft_ || r != lastSideLightRight_) {
        lastSideLightLeft_  = l;
        lastSideLightRight_ = r;
        updateMatchSideLights(fdn, l, r);
    }
}

void SymbolIdle::renderSymbolScreen(FDN* fdn) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen();

    if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK))           d->whiteScreenLeftHalf();
    if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY)) d->whiteScreenRightHalf();

    d->setGlyphMode(FontMode::SYMBOL_GLYPH);

    if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK) || !leftConnected || blinkToggle)
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK), 24, 40);

    if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY) || !rightConnected || blinkToggle)
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK_SECONDARY), 72, 40);

    d->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);

    int timeLeft = symbolManager->getTimeLeftToRefresh();
    int minutes  = timeLeft / 60000;
    int seconds  = (timeLeft % 60000) / 1000;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    d->drawText(buf, 40, 64);

    d->render();
}

bool SymbolIdle::transitionToSelection() {
    return transitionToSelectionState;
}

bool SymbolIdle::transitionToMatchSuccess() {
    return symbolManager->isMatched(SerialIdentifier::INPUT_JACK)
        && symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY)
        && symbolSentLeft && symbolSentRight;
}

bool SymbolIdle::transitionToMainMenu() {
    return transitionToMainMenuApp;
}
