#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
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