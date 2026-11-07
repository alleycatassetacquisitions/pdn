#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "device/animation/idle-animation.hpp"
#include "device/drivers/logger.hpp"
#include <functional>


static const char* TAG = "SymbolState";

SymbolState::SymbolState(const GameContext& ctx)
    : ConnectState<PDN>(ctx.remoteDeviceCoordinator, SYMBOL) {
    this->player = ctx.player;
    this->matchManager = ctx.matchManager;
    this->symbolWirelessManager = ctx.symbolWirelessManager;
}

SymbolState::~SymbolState() {
    this->player = nullptr;
    this->matchManager = nullptr;
    this->symbolWirelessManager = nullptr;
}

void SymbolState::onStateMounted(PDN* pdn) {
    LOG_W(TAG, "mounted");
    mountedPdn = pdn;
    fdnMac = nullptr;
    phase = Phase::ACTIVE;
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
        phase = Phase::TO_IDLE;
    }

    parameterizedCallbackFunction sendSymbolToFDN = [](void *ctx) {
        SymbolState* symbolState = (SymbolState*)ctx;
        symbolState->sendSymbolToFDN();
    };

    pdn->getSecondaryButton()->setButtonPress(sendSymbolToFDN, this, ButtonInteraction::CLICK);

    symbolWirelessManager->setPacketReceivedCallback(
        std::bind(&SymbolState::onSymbolMatchCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::OUTPUT_JACK);

    pdn->getLightManager()->stopAnimation();

    LEDState allWhite;
    for (int i = 0; i < 9; ++i) {
        allWhite.leftLights[i]  = LEDState::SingleLEDState(LEDColor(255, 255, 255), 100);
        allWhite.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 255, 255), 100);
    }
    allWhite.transmitLight = LEDState::SingleLEDState(LEDColor(255, 255, 255), 100);

    cfg.loop = true;
    cfg.speed = 255;  // AnimationConfig::speed is uint8_t
    cfg.initialState = allWhite;
    
}

void SymbolState::onStateLoop(PDN* pdn) {
    if (phase == Phase::TO_IDLE) {
        return;
    }

    if (hapticPulseActive && hapticPulseTimer.expired()) {
        pdn->getHaptics()->off();
        hapticPulseTimer.invalidate();
        hapticPulseActive = false;
    }

    if (phase != Phase::EXIT_DWELL && remoteDeviceCoordinator->getPeerDeviceType(
                                      SerialIdentifier::OUTPUT_JACK) != DeviceType::FDN) {
        phase = Phase::EXIT_DWELL;
        renderTimer.invalidate();
        bufferTimer.setTimer(BUFFER_TIMEOUT);
    }

    if (phase != Phase::EXIT_DWELL && symbolSent) {
        if (!hapticPulseActive) {
            pdn->getHaptics()->setIntensity(VIBRATION_MAX);
            hapticPulseTimer.setTimer(HAPTIC_PULSE_DURATION);
            hapticPulseActive = true;
        }

        // sendSymbolToFDN();
        advanceSymbolRender(pdn);
        renderTimer.invalidate();
    }

    // Buffer animation must not block the main loop — sync needs to run every tick.
    if (bufferTimer.isRunning()) {
        if (bufferTimer.expired()) {
            bufferTimer.invalidate();
            if (phase == Phase::EXIT_DWELL) {
                phase = Phase::TO_IDLE;
                return;
            }
            advanceSymbolRender(pdn);
        } else {
            if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
                renderLoadingScreen(pdn->getDisplay());
            }
            return;
        }
    }

    if (renderTimer.expired()) {
        advanceSymbolRender(pdn);
    }
}

void SymbolState::onStateDismounted(PDN* pdn) {
    LOG_W(TAG, "dismounted");
    mountedPdn = nullptr;
    fdnMac = nullptr;
    phase = Phase::ACTIVE;
    symbolSent = false;
    hapticPulseActive = false;
    bufferTimer.invalidate();
    hapticPulseTimer.invalidate();
    pdn->getHaptics()->off();
    if (renderTimer.isRunning()) {
        renderTimer.invalidate();
    }

    symbolWirelessManager->clearCallback();

    matchReady = false;

    pdn->getLightManager()->stopAnimation();
    pdn->getLightManager()->clear();
}

bool SymbolState::isPrimaryRequired() {
    return true;
}

bool SymbolState::isAuxRequired() {
    return true;
}

bool SymbolState::transitionToIdle() { return phase == Phase::TO_IDLE; }

bool SymbolState::transitionToSymbolMatched() { return phase == Phase::TO_MATCHED; }

void SymbolState::renderSymbolScreen(PDN* pdn) {
    
    
}

void SymbolState::advanceSymbolRender(PDN* pdn) {
    LOG_W(TAG, "advanceSymbolRender: symbolSent=%d, toggleSymbol=%d", symbolSent, toggleSymbol);
    if (!symbolSent) {
        if (toggleSymbol) {
            pdn->getDisplay()->invalidateScreen();
            pdn->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH)->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);

            pdn->getDisplay()->render();

            pdn->getLightManager()->startAnimation(new IdleAnimation(), cfg);
        } else {
            pdn->getDisplay()->invalidateScreen();
            pdn->getDisplay()->render();

            pdn->getLightManager()->stopAnimation();
        }
        toggleSymbol = !toggleSymbol;
        renderTimer.setTimer(RENDER_TIMEOUT);
    } else {
        pdn->getDisplay()->invalidateScreen();
        pdn->getDisplay()->whiteScreen();
        pdn->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH)->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);

        pdn->getDisplay()->render();
        symbolSent = false;

        pdn->getLightManager()->startAnimation(new IdleAnimation(), cfg);
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
    if (phase == Phase::EXIT_DWELL || phase == Phase::TO_IDLE) {
        return;
    }

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
        phase = Phase::ACTIVE;

        // Force an immediate redraw back to the blinking symbol.
        renderTimer.invalidate();
        bufferTimer.invalidate();
        if (mountedPdn != nullptr) {
            advanceSymbolRender(mountedPdn);
            renderTimer.setTimer(RENDER_TIMEOUT);
        }
    } else if (command.command == SMCommand::SYMBOL_MATCH_SUCCESS) {
        phase = Phase::TO_MATCHED;
    }
}
