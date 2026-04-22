#include <FastLED.h>

#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatched";

SymbolMatched::SymbolMatched(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager) : ConnectState(remoteDeviceCoordinator, SYMBOL_MATCHED) {
    this->player = player;
    this->symbolWirelessManager = symbolWirelessManager;
}

SymbolMatched::~SymbolMatched() {
    this->player = nullptr;
}

void SymbolMatched::onStateMounted(Device *PDN) {
    LOG_W(TAG, "mounted");
    transitionToSymbolState = false;
    transitionToIdleState = false;
    toggleBlink = true;
    holdSteadySymbol = false;
    renderSymbolScreen(PDN);
    blinkTimer.setTimer(BLINK_INTERVAL);
    successTimer.setTimer(SUCCESS_TIMEOUT);

    symbolWirelessManager->setPacketReceivedCallback(
        std::bind(&SymbolMatched::onSymbolMatchCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::OUTPUT_JACK);

    cfg.type = player->isHunter() ? AnimationType::HUNTER_WIN : AnimationType::BOUNTY_WIN;
    cfg.loop = true;
    cfg.speed = 16;
    cfg.initialState = LEDState();
    cfg.loopDelayMs = 0;

    PDN->getLightManager()->startAnimation(cfg);
}

void SymbolMatched::onStateLoop(Device *PDN) {
    if (!(getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::FDN)) {
        transitionToIdleState = true;
        return;
    }

    if (!holdSteadySymbol) {
        if (blinkTimer.expired()) {
            renderSymbolScreen(PDN);
            blinkTimer.setTimer(BLINK_INTERVAL);
        }

        if (successTimer.expired()) {
            holdSteadySymbol = true;
            PDN->getHaptics()->off();
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->whiteScreen();
            PDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
            PDN->getDisplay()->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);
            PDN->getDisplay()->render();
            blinkTimer.invalidate();
            successTimer.invalidate();
        }
    }
}

void SymbolMatched::onStateDismounted(Device *PDN) {
    LOG_W(TAG, "dismounted");
    const bool showRefreshScreen = transitionToIdleState && !transitionToSymbolState;
    transitionToSymbolState = false;
    transitionToIdleState = false;
    toggleBlink = true;
    holdSteadySymbol = false;
    blinkTimer.invalidate();
    successTimer.invalidate();


    PDN->getLightManager()->stopAnimation();
    PDN->getLightManager()->clear();
    PDN->getHaptics()->off();
    // clear() updates the FastLED buffer; show() normally runs in the light driver's
    // exec() on a timer. This loop blocks the main loop, so push pixels now before loading UI.
    FastLED.show();

    if (showRefreshScreen) {
        SimpleTimer bufferTimer;
        const int bufferTimeout = 1000;
        bufferTimer.setTimer(bufferTimeout);
        while (!bufferTimer.expired()) {
            if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
                renderLoadingScreen(PDN->getDisplay());
            }
        }
        bufferTimer.invalidate();
    }

    symbolWirelessManager->clearCallback();
}

bool SymbolMatched::transitionToSymbol() {
    return transitionToSymbolState;
}

bool SymbolMatched::transitionToIdle() {
    return transitionToIdleState;
}

void SymbolMatched::renderSymbolScreen(Device *PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->whiteScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (toggleBlink) {
        PDN->getDisplay()->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);
        PDN->getHaptics()->setIntensity(VIBRATION_MAX);
    } else {
        PDN->getHaptics()->off();
    }
    PDN->getDisplay()->render();
    toggleBlink = !toggleBlink;
}

void SymbolMatched::onSymbolMatchCommandReceived(SymbolMatchCommand command) {
    if (command.command == SMCommand::SYMBOLS_REFRESHED) {
        transitionToSymbolState = true;
    }
}

bool SymbolMatched::isPrimaryRequired() {
    return true;
}

bool SymbolMatched::isAuxRequired() {
    return true;
}