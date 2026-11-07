#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/animation/bounty-win-animation.hpp"

static const char* TAG = "SymbolMatched";

SymbolMatched::SymbolMatched(const GameContext& ctx)
    : ConnectState<PDN>(ctx.remoteDeviceCoordinator, SYMBOL_MATCHED) {
    this->player = ctx.player;
    this->symbolWirelessManager = ctx.symbolWirelessManager;
}

SymbolMatched::~SymbolMatched() {
    this->player = nullptr;
}

void SymbolMatched::onStateMounted(PDN* pdn) {
    LOG_W(TAG, "mounted");
    phase = Phase::BLINKING;
    toggleBlink = true;
    bufferTimer.invalidate();
    renderSymbolScreen(pdn);
    blinkTimer.setTimer(BLINK_INTERVAL);
    successTimer.setTimer(SUCCESS_TIMEOUT);

    symbolWirelessManager->setPacketReceivedCallback(
        std::bind(&SymbolMatched::onSymbolMatchCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::OUTPUT_JACK);

    AnimationBase* animation = player->isHunter()
        ? (AnimationBase*)new HunterWinAnimation()
        : (AnimationBase*)new BountyWinAnimation();
    cfg.loop = true;
    cfg.speed = 16;
    cfg.initialState = LEDState();
    cfg.loopDelayMs = 0;

    pdn->getLightManager()->startAnimation(animation, cfg);
}

void SymbolMatched::onStateLoop(PDN* pdn) {
    if (phase == Phase::TO_SYMBOL || phase == Phase::TO_IDLE) {
        return;
    }

    if (phase != Phase::EXIT_DWELL &&
        getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) != DeviceType::FDN) {
        phase = Phase::EXIT_DWELL;
        pdn->getHaptics()->off();
        blinkTimer.invalidate();
        successTimer.invalidate();
        bufferTimer.setTimer(BUFFER_TIMEOUT);
    }

    if (phase == Phase::EXIT_DWELL) {
        if (bufferTimer.expired()) {
            bufferTimer.invalidate();
            phase = Phase::TO_IDLE;
        } else if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            renderLoadingScreen(pdn->getDisplay());
        }
        return;
    }

    if (phase == Phase::BLINKING) {
        if (blinkTimer.expired()) {
            renderSymbolScreen(pdn);
            blinkTimer.setTimer(BLINK_INTERVAL);
        }

        if (successTimer.expired()) {
            phase = Phase::STEADY;
            pdn->getHaptics()->off();
            pdn->getDisplay()->invalidateScreen();
            pdn->getDisplay()->whiteScreen();
            pdn->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
            pdn->getDisplay()->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);
            pdn->getDisplay()->render();
            blinkTimer.invalidate();
            successTimer.invalidate();
        }
    }
}

void SymbolMatched::onStateDismounted(PDN* pdn) {
    LOG_W(TAG, "dismounted");
    phase = Phase::BLINKING;
    toggleBlink = true;
    blinkTimer.invalidate();
    successTimer.invalidate();
    bufferTimer.invalidate();

    pdn->getLightManager()->stopAnimation();
    pdn->getLightManager()->clear();
    pdn->getHaptics()->off();

    symbolWirelessManager->clearCallback();
}

bool SymbolMatched::isPrimaryRequired() {
    return true;
}

bool SymbolMatched::isAuxRequired() {
    return true;
}

bool SymbolMatched::transitionToSymbol() { return phase == Phase::TO_SYMBOL; }

bool SymbolMatched::transitionToIdle() { return phase == Phase::TO_IDLE; }

void SymbolMatched::renderSymbolScreen(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen();
    pdn->getDisplay()->whiteScreen();
    pdn->getDisplay()->setGlyphMode(FontMode::SYMBOL_GLYPH);
    if (toggleBlink) {
        pdn->getDisplay()->renderGlyph(player->getSymbol()->getSymbolGlyph(), 48, 48);
        pdn->getHaptics()->setIntensity(VIBRATION_MAX);
    } else {
        pdn->getHaptics()->off();
    }
    pdn->getDisplay()->render();
    toggleBlink = !toggleBlink;
}

void SymbolMatched::onSymbolMatchCommandReceived(SymbolMatchCommand command) {
    if (phase == Phase::EXIT_DWELL || phase == Phase::TO_IDLE) {
        return;
    }
    if (command.command == SMCommand::SYMBOLS_REFRESHED) {
        phase = Phase::TO_SYMBOL;
    }
}
