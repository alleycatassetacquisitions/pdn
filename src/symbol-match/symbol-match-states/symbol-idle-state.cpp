#include <cstring>

#include "device/remote-device-coordinator.hpp"
#include "symbol-match/symbol-match-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SymbolMatch";

SymbolIdle::SymbolIdle(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
                       SymbolWirelessManager* symbolWirelessManager)
    : ConnectState(remoteDeviceCoordinator, SYMBOL_IDLE) {
    this->symbolManager = symbolManager;
    this->symbolWirelessManager = symbolWirelessManager;
}

SymbolIdle::~SymbolIdle() {
    symbolWirelessManager = nullptr;
}

void SymbolIdle::onStateMounted(Device *FDN) {
    LOG_W(TAG, "SymbolIdle mounted");

    mountedFdn = FDN;

    renderSymbolScreen(FDN);

    transitionToRightConnectedState = false;

    symbolWirelessManager->setPacketReceivedCallback(std::bind(&SymbolIdle::onSymbolMatchCommandReceived, this, std::placeholders::_1));

    parameterizedCallbackFunction refreshSymbols = [](void *ctx) {
        SymbolIdle* symbolIdleState = (SymbolIdle*)ctx;
        symbolIdleState->symbolManager->refreshSymbols();
    };

    FDN->getSecondaryButton()->setButtonPress(refreshSymbols, this, ButtonInteraction::CLICK);
}

void SymbolIdle::onStateLoop(Device *FDN) {
    if (symbolManager->getRefreshTimer()->expired()) {
        transitionToSelectionState = true;
        LOG_W(TAG, "SymbolIdle: transitionToSelectionState = true");
        return;
    } 

    leftConnected = remoteDeviceCoordinator->getPortStatus(SerialIdentifier::OUTPUT_JACK) == PortStatus::CONNECTED;
    rightConnected = remoteDeviceCoordinator->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED;

    if (!leftConnected) {
        symbolManager->setLeftMatched(false);
        renderSymbolScreen(mountedFdn);
    }
    if (!rightConnected) {
        symbolManager->setRightMatched(false);
        renderSymbolScreen(mountedFdn);
    }

    if (!symbolSentLeft && leftConnected) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
        if (peerMac != nullptr) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SEND_SYMBOL,
                symbolManager->getSymbol(SymbolPosition::LEFT)->getSymbolId(),
                SerialIdentifier::OUTPUT_JACK);
            symbolSentLeft = true;
        }
    } else if (!leftConnected) {
        symbolSentLeft = false;
    }

    if (!symbolSentRight && rightConnected) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::INPUT_JACK);
        if (peerMac != nullptr) {
            symbolWirelessManager->setMacPeer(peerMac);
            symbolWirelessManager->sendPacket(SMCommand::SEND_SYMBOL,
                symbolManager->getSymbol(SymbolPosition::RIGHT)->getSymbolId(),
                SerialIdentifier::INPUT_JACK);
            symbolSentRight = true;
        }
    } else if (!rightConnected) {
        symbolSentRight = false;
    }

    if (symbolManager->getRefreshTimer()->isRunning()) {
        int elapsedTime = symbolManager->getRefreshTimer()->getElapsedTime();
        if (elapsedTime - lastTimeRendered >= 500) {
            lastTimeRendered = elapsedTime;
            renderSymbolScreen(FDN);
            blinkToggle = !blinkToggle;
        }
    }
}

void SymbolIdle::onStateDismounted(Device *FDN) {
    transitionToSelectionState = false;
    transitionToLeftConnectedState = false;
    transitionToRightConnectedState = false;
    LOG_W(TAG, "SymbolIdle dismounted");

    mountedFdn = nullptr;
    lastTimeRendered = 0;

    symbolWirelessManager->clearCallback();

    symbolManager->setLeftMatched(false);
    symbolManager->setRightMatched(false);
}

void SymbolIdle::renderSymbolScreen(Device *FDN) {
    FDN->getDisplay()->invalidateScreen();

    // render symbol glyphs
    if (symbolManager->isLeftMatched()) {
        FDN->getDisplay()->whiteScreenLeftHalf();
    }

    if (symbolManager->isRightMatched()) {
        FDN->getDisplay()->whiteScreenRightHalf();
    }

    // Half-screen fills reset draw color / font mode; SYMBOL_GLYPH uses XOR (draw color 2) + transparent font
    FDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);

    if (symbolManager->isLeftMatched() || !leftConnected || blinkToggle) {
        FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::LEFT), 24, 40);
    } 

    if (symbolManager->isRightMatched() || !rightConnected || blinkToggle) {
        FDN->getDisplay()->renderGlyph(symbolManager->getSymbolGlyph(SymbolPosition::RIGHT), 72, 40);
    }

    FDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);

    int timeLeft = symbolManager->getTimeLeftToRefresh();
    
    int minutes = timeLeft / 60000;
    int seconds = (timeLeft % 60000) / 1000;

    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    FDN->getDisplay()->drawText(buffer, 40, 64);

    FDN->getDisplay()->render();
}

void SymbolIdle::onSymbolMatchCommandReceived(SymbolMatchCommand command) {
    if (command.command != SMCommand::SEND_SYMBOL) {
        return;
    }

    if (command.serialPort == SerialIdentifier::OUTPUT_JACK) {
        symbolManager->setLeftMatched(true);
    } else if (command.serialPort == SerialIdentifier::INPUT_JACK) {
        symbolManager->setRightMatched(true);
    }

    if (mountedFdn != nullptr) {
        renderSymbolScreen(mountedFdn);
    }
}

bool SymbolIdle::transitionToSelection() {
    return transitionToSelectionState;
}

bool SymbolIdle::transitionToLeftConnected() {
    return transitionToLeftConnectedState;
}

bool SymbolIdle::transitionToRightConnected() {
    return transitionToRightConnectedState;
}

bool SymbolIdle::isPrimaryRequired() {
    return true;
}

bool SymbolIdle::isAuxRequired() {
    return true;
}
