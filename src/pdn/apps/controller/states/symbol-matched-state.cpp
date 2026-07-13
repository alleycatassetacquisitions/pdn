#include "apps/controller/controller-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/animation/animation-base.hpp"
#include "game/quickdraw-resources.hpp"
#include <random>

static const char* TAG = "SymbolMatched";

namespace {

class MatchSuccessAnimation : public AnimationBase {
protected:
    void onInit() override {
        currentState_ = config_.initialState;
        currentState_.clear();
    }

    LEDState onAnimate() override {
        for (uint8_t i = 0; i < 9; ++i) {
            currentState_.leftLights[i].brightness =
                static_cast<uint8_t>(currentState_.leftLights[i].brightness * 92 / 100);
            currentState_.rightLights[i].brightness =
                static_cast<uint8_t>(currentState_.rightLights[i].brightness * 92 / 100);
        }
        static constexpr LEDColor kWhite(255, 255, 255);
        static constexpr LEDColor kGold(255, 180, 0);
        if (rng_() % 3 == 0) {
            uint8_t idx  = static_cast<uint8_t>(rng_() % 9);
            bool    left = (rng_() % 2 == 0);
            LEDColor col = (rng_() % 2 == 0) ? kWhite : kGold;
            if (left) currentState_.leftLights[idx]  = LEDState::SingleLEDState(col, 255);
            else      currentState_.rightLights[idx] = LEDState::SingleLEDState(col, 255);
        }
        return currentState_;
    }

private:
    std::minstd_rand rng_{42};
};

}  // namespace

SymbolMatchedState::SymbolMatchedState(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager, ControllerWirelessManager* controllerWirelessManager) : ConnectState<PDN>(remoteDeviceCoordinator, SYMBOL_MATCHED) {
    this->player = player;
    this->symbolWirelessManager = symbolWirelessManager;
    this->controllerWirelessManager = controllerWirelessManager;
}

SymbolMatchedState::~SymbolMatchedState() {
    this->player = nullptr;
    this->symbolWirelessManager = nullptr;
    this->controllerWirelessManager = nullptr;
}

void SymbolMatchedState::onStateMounted(PDN* pdn) {
    LOG_W(TAG, "mounted");
    transitionToSymbolState = false;
    transitionToIdleState = false;
    transitionToController1State = false;
    toggleBlink = true;
    holdSteadySymbol = false;
    renderSymbolScreen(pdn);
    blinkTimer.setTimer(BLINK_INTERVAL);
    successTimer.setTimer(SUCCESS_TIMEOUT);

    symbolWirelessManager->setPacketReceivedCallback(
        std::bind(&SymbolMatchedState::onSymbolMatchCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::OUTPUT_JACK);

    cfg.loop = true;
    cfg.speed = 20;
    cfg.initialState = LEDState();
    cfg.loopDelayMs = 0;

    pdn->getLightManager()->startAnimation(new MatchSuccessAnimation(), cfg);

    if (getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::FDN) {
        const uint8_t* fdnMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
        if (fdnMac != nullptr) {
            controllerWirelessManager->setMacPeer(fdnMac);
        }
    }

    controllerWirelessManager->setGameSelectReceivedCallback(
        std::bind(&SymbolMatchedState::onGameSelectCommandReceived, this, std::placeholders::_1));
}

void SymbolMatchedState::onStateLoop(PDN* pdn) {
    if (!(getPeerDeviceType(SerialIdentifier::OUTPUT_JACK) == DeviceType::FDN)) {
        transitionToIdleState = true;
        return;
    }

    if (!holdSteadySymbol) {
        if (blinkTimer.expired()) {
            renderSymbolScreen(pdn);
            blinkTimer.setTimer(BLINK_INTERVAL);
        }

        if (successTimer.expired()) {
            holdSteadySymbol = true;
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

void SymbolMatchedState::onStateDismounted(PDN* pdn) {
    LOG_W(TAG, "dismounted");
    const bool showRefreshScreen = transitionToIdleState && !transitionToSymbolState;
    transitionToSymbolState = false;
    transitionToIdleState = false;
    transitionToController1State = false;
    toggleBlink = true;
    holdSteadySymbol = false;
    blinkTimer.invalidate();
    successTimer.invalidate();

    pdn->getLightManager()->stopAnimation();
    pdn->getLightManager()->clear();
    pdn->getHaptics()->off();

    if (showRefreshScreen) {
        SimpleTimer bufferTimer;
        const int bufferTimeout = 1000;
        bufferTimer.setTimer(bufferTimeout);
        while (!bufferTimer.expired()) {
            if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
                renderLoadingScreen(pdn->getDisplay());
            }
        }
        bufferTimer.invalidate();
    }

    symbolWirelessManager->clearCallback();
}

bool SymbolMatchedState::isPrimaryRequired() {
    return true;
}

bool SymbolMatchedState::isAuxRequired() {
    return true;
}

bool SymbolMatchedState::transitionToSymbol() {
    return transitionToSymbolState;
}

bool SymbolMatchedState::transitionToIdle() {
    return transitionToIdleState;
}

bool SymbolMatchedState::transitionToController1() {
    return transitionToController1State;
}

void SymbolMatchedState::renderSymbolScreen(PDN* pdn) {
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

void SymbolMatchedState::onSymbolMatchCommandReceived(SymbolMatchCommand command) {
    if (command.command == SMCommand::SYMBOLS_REFRESHED) {
        transitionToSymbolState = true;
    }
}

void SymbolMatchedState::onGameSelectCommandReceived(GameSelectCommand command) {
    if (command.command != GameSelectCmd::GAME_SELECT) {
        return;
    }

    switch (command.gameId) {
        case GameSelectId::CONTROLLER_1:
            transitionToController1State = true;
            break;
        default:
            break;
    }
}