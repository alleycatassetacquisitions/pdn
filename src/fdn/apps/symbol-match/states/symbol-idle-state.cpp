#include <algorithm>
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
    , symbolWirelessManager(symbolWirelessManager) {
    setOnJackState([this](SerialIdentifier jack, const JackConnectionState& state) {
        onInputJackState(jack, state.connected);
    });
}

void SymbolIdle::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    mountedFdn = fdn;
    renderSymbolScreen(fdn);

    for (SerialIdentifier jack :
         {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        symbolWirelessManager->setPacketReceivedCallback(
        [this, jack](const SymbolMatchCommand& command) {
            const bool sideConnected = isJackConnected(jack);
            // A straggler from a departed peer must not light a side nobody is cabled to.
            if (command.command != SMCommand::SEND_SYMBOL || !sideConnected) return;
            const SymbolId local = symbolManager->getSymbol(jack)->getSymbolId();
            symbolManager->setMatched(jack, command.symbolId == local);
            if (mountedFdn) {
                renderSymbolScreen(mountedFdn);
                syncMatchSideLights(mountedFdn);
            }
        },
        jack);
    }

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

    if (symbolManager->getRefreshTimer()->isRunning()) {
        int elapsed = symbolManager->getRefreshTimer()->getElapsedTime();
        if (elapsed - lastTimeRendered >= 500) {
            lastTimeRendered = elapsed;
            renderSymbolScreen(fdn);
            blinkToggle = !blinkToggle;
        }
    }
}

void SymbolIdle::onInputJackState(SerialIdentifier jack, bool connected) {
    if (jack != SerialIdentifier::INPUT_JACK && jack != SerialIdentifier::INPUT_JACK_SECONDARY)
        return;
    const bool isLeft = jack == SerialIdentifier::INPUT_JACK;
    bool& symbolSent = isLeft ? symbolSentLeft : symbolSentRight;

    if (connected) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(jack);
        if (peerMac == nullptr) {
            LOG_E(TAG, "No peer MAC on connected jack %d", static_cast<int>(jack));
            return;
        }
        symbolWirelessManager->setMacPeer(peerMac);
        symbolWirelessManager->sendPacket(SMCommand::SEND_SYMBOL,
                                          symbolManager->getSymbol(jack)->getSymbolId(), jack);
        symbolSent = true;
    } else {
        symbolManager->setMatched(jack, false);
        symbolSent = false;
        if (mountedFdn) renderSymbolScreen(mountedFdn);
    }

    if (mountedFdn) syncMatchSideLights(mountedFdn);
}

void SymbolIdle::onStateDismounted(FDN* fdn) {
    transitionToSelectionState = false;
    mountedFdn                 = nullptr;
    lastTimeRendered           = 0;
    symbolSentLeft             = false;
    symbolSentRight = false;

    symbolWirelessManager->clearCallback();
    symbolManager->setMatched(SerialIdentifier::INPUT_JACK, false);
    symbolManager->setMatched(SerialIdentifier::INPUT_JACK_SECONDARY, false);

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

    if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK) ||
        !isJackConnected(SerialIdentifier::INPUT_JACK) || blinkToggle)
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK), 24, 40);

    if (symbolManager->isMatched(SerialIdentifier::INPUT_JACK_SECONDARY) ||
        !isJackConnected(SerialIdentifier::INPUT_JACK_SECONDARY) || blinkToggle)
        d->renderGlyph(symbolManager->getSymbolGlyph(SerialIdentifier::INPUT_JACK_SECONDARY), 72, 40);

    d->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);

    // Elapsed can pass the interval before the refresh fires, so floor at zero.
    // The 99 cap bounds %02d for -Wformat-truncation; the interval is far shorter.
    int timeLeft = std::max(0, symbolManager->getTimeLeftToRefresh());
    int minutes = std::min(timeLeft / 60000, 99);
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
