#include "device/device-constants.hpp"
#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include <functional>


static const char* TAG = "SymbolState";

SymbolState::SymbolState(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager) : ConnectState(remoteDeviceCoordinator, SYMBOL) {
    this->player = player;
    this->matchManager = matchManager;
    this->symbolWirelessManager = symbolWirelessManager;
}

SymbolState::~SymbolState() {
    this->player = nullptr;
    this->matchManager = nullptr;
    this->symbolWirelessManager = nullptr;
}

void SymbolState::onStateMounted(Device *PDN) {
    LOG_W(TAG, "mounted");
    mountedPdn = PDN;
    transitionToIdleState = false;
    transitionToSymbolMatchedState = false;
    matchReady = false;
    symbolSent = false;
    hapticPulseActive = false;
    toggleSymbol = true;

    if (remoteDeviceCoordinator->getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::FDN) {
        fdnMac = const_cast<uint8_t*>(remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK));
    }

    if (fdnMac != nullptr) {
        bufferTimer.setTimer(BUFFER_TIMEOUT);
        symbolWirelessManager->setMacPeer(fdnMac);
    } else {
        transitionToIdleState = true;
    }

    parameterizedCallbackFunction sendSymbolToFDN = [](void *ctx) {
        SymbolState* symbolState = (SymbolState*)ctx;
        symbolState->sendSymbolToFDN();
    };

    PDN->getSecondaryButton()->setButtonPress(sendSymbolToFDN, this, ButtonInteraction::CLICK);

    symbolWirelessManager->setPacketReceivedCallback(
        std::bind(&SymbolState::onSymbolMatchCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::OUTPUT_JACK);

    PDN->getLightManager()->stopAnimation();

    LEDState allWhite;
    for (int i = 0; i < 9; ++i) {
        allWhite.leftLights[i]  = LEDState::SingleLEDState(LEDColor(255, 255, 255), 100);
        allWhite.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 255, 255), 100);
    }
    allWhite.transmitLight = LEDState::SingleLEDState(LEDColor(255, 255, 255), 100);

    cfg.type = AnimationType::IDLE;
    cfg.loop = true;
    cfg.speed = 255;  // AnimationConfig::speed is uint8_t
    cfg.initialState = allWhite;
    
}

void SymbolState::onStateLoop(Device *PDN) {
    if (symbolSent) {
        if (!hapticPulseActive) {
            PDN->getHaptics()->setIntensity(VIBRATION_MAX);
            hapticPulseTimer.setTimer(HAPTIC_PULSE_DURATION);
            hapticPulseActive = true;
        }

        // sendSymbolToFDN();
        advanceSymbolRender(PDN);
        renderTimer.invalidate();
    }

    if (hapticPulseActive && hapticPulseTimer.expired()) {
        PDN->getHaptics()->off();
        hapticPulseTimer.invalidate();
        hapticPulseActive = false;
    }

    // Buffer animation must not block the main loop — handshake/sync needs to run every tick.
    if (bufferTimer.isRunning()) {
        if (bufferTimer.expired()) {
            bufferTimer.invalidate();
            advanceSymbolRender(PDN);
        } else {
            if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
                renderLoadingScreen(PDN->getDisplay());
            }
            return;
        }
    }

    if (renderTimer.expired()) {
        advanceSymbolRender(PDN);
    }

    // if device is not connected to an FDN, transition to idle
    if (remoteDeviceCoordinator->getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) != DeviceType::FDN) {
        transitionToIdleState = true;
    }
}

void SymbolState::onStateDismounted(Device *PDN) {
    LOG_W(TAG, "dismounted");
    const bool showRefreshScreen = transitionToIdleState && !transitionToSymbolMatchedState;
    mountedPdn = nullptr;
    fdnMac = nullptr;
    transitionToIdleState = false;
    transitionToSymbolMatchedState = false;
    symbolSent = false;
    hapticPulseActive = false;
    bufferTimer.invalidate();
    hapticPulseTimer.invalidate();
    PDN->getHaptics()->off();
    if (renderTimer.isRunning()) {
        renderTimer.invalidate();
    }

    if (showRefreshScreen) {
        bufferTimer.setTimer(BUFFER_TIMEOUT);
        while (!bufferTimer.expired()) {
            if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
                renderLoadingScreen(PDN->getDisplay());
            }
        }
        bufferTimer.invalidate();
    }

    symbolWirelessManager->clearCallback();

    matchReady = false;

    PDN->getLightManager()->stopAnimation();
    PDN->getLightManager()->clear();
}

bool SymbolState::isPrimaryRequired() {
    return true;
}

bool SymbolState::isAuxRequired() {
    return true;
}

bool SymbolState::transitionToIdle() {
    return transitionToIdleState;
}

bool SymbolState::transitionToSymbolMatched() {
    return transitionToSymbolMatchedState;
}

void SymbolState::renderSymbolScreen(Device *PDN) {
    
    
}

void SymbolState::advanceSymbolRender(Device* PDN) {
    LOG_W(TAG, "advanceSymbolRender: symbolSent=%d, toggleSymbol=%d", symbolSent, toggleSymbol);
    if (!symbolSent) {
        if (toggleSymbol) {
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH)->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);

            PDN->getDisplay()->render();

            PDN->getLightManager()->startAnimation(cfg);
        } else {
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->render();

            PDN->getLightManager()->stopAnimation();
        }
        toggleSymbol = !toggleSymbol;
        renderTimer.setTimer(RENDER_TIMEOUT);
    } else {
        PDN->getDisplay()->invalidateScreen();
        PDN->getDisplay()->whiteScreen();
        PDN->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH)->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);

        PDN->getDisplay()->render();
        symbolSent = false;

        PDN->getLightManager()->startAnimation(cfg);
    }
}

void SymbolState::sendSymbolToFDN() {
    if (matchReady) {
        symbolSent = true;
        symbolWirelessManager->sendPacket(
            SMCommand::SEND_SYMBOL,
            player->getSymbol()->getSymbolId(),
            SerialIdentifier::OUTPUT_JACK);
    } else {
        // trigger rejection behavior
    }
}

void SymbolState::onSymbolMatchCommandReceived(SymbolMatchCommand command) {

    if (command.command == SMCommand::SEND_SYMBOL) {
        fdnSymbol = command.symbolId;
        if (fdnSymbol == player->getSymbol()->getSymbolId()) {
            matchReady = true;
        }
        LOG_W(TAG, "Comparison: fdnSymbol=%d, playerSymbol=%d, matchReady=%d", 
              static_cast<int>(fdnSymbol), 
              static_cast<int>(player->getSymbol()->getSymbolId()), 
              matchReady ? 1 : 0);
   

        LOG_W(TAG, "Received symbol: %d", static_cast<int>(fdnSymbol));
    } else if (command.command == SMCommand::SYMBOLS_REFRESHED) {
        symbolSent = false;
        matchReady = false;
        toggleSymbol = true;
        transitionToSymbolMatchedState = false;

        // Force an immediate redraw back to the blinking symbol.
        renderTimer.invalidate();
        bufferTimer.invalidate();
        if (mountedPdn != nullptr) {
            advanceSymbolRender(mountedPdn);
            renderTimer.setTimer(RENDER_TIMEOUT);
        }
    } else if (command.command == SMCommand::SYMBOL_MATCH_SUCCESS) {
        transitionToSymbolMatchedState = true;
    }
}
