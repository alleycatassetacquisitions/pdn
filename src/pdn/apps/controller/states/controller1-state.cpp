#include "apps/controller/controller-states.hpp"
#include "device/device.hpp"
#include "device/drivers/button.hpp"
#include "device/drivers/logger.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/animation-base.hpp"
#include "device/animation/idle-animation.hpp"
#include "device/animation/vertical-chase-animation.hpp"
#include "device/animation/transmit-breath-animation.hpp"
#include "device/animation/countdown-animation.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/animation/bounty-win-animation.hpp"
#include "device/animation/lose-animation.hpp"
#include "game/peripheral-glyphs.hpp"
#include "game/quickdraw-countdown.hpp"
#include "symbol.hpp"

namespace {
static const char* TAG = "Controller1State";

class TransmitOnAnimation : public AnimationBase {
protected:
    void onInit() override { currentState_ = config_.initialState; }
    LEDState onAnimate() override { return currentState_; }
};

}  // namespace

Controller1State::Controller1State(ControllerWirelessManager* controllerWirelessManager) : TypedState<PDN>(ControllerStateId::CONTROLLER1) {
    this->controllerWirelessManager = controllerWirelessManager;
}

Controller1State::~Controller1State() {
    this->controllerWirelessManager = nullptr;
}

void Controller1State::onStateMounted(PDN* pdn) {
    LOG_W(TAG, "Mounted");
    pdn->getDisplay()->invalidateScreen()->render();

    // Register all ButtonInteraction types for both buttons.
    // Each interaction gets its own ButtonCallbackCtx so the callback knows
    // which interaction to forward to the FDN.
    static parameterizedCallbackFunction kDispatch = [](void* ctx) {
        auto* bctx = static_cast<ButtonCallbackCtx*>(ctx);
        bctx->state->sendButtonMessage(bctx->buttonId, bctx->interaction);
    };

    Button* buttons[2] = {pdn->getPrimaryButton(), pdn->getSecondaryButton()};
    ButtonIdentifier ids[2] = {ButtonIdentifier::PRIMARY_BUTTON, ButtonIdentifier::SECONDARY_BUTTON};

    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < kNumInteractions; ++i) {
            auto interaction = static_cast<ButtonInteraction>(i);
            // DURING_LONG_PRESS fires every loop tick while held — sending a
            // packet on every tick floods the wireless channel and crashes both
            // devices. Skip it; LONG_PRESS (fires once) is sufficient.
            if (interaction == ButtonInteraction::DURING_LONG_PRESS) continue;
            int ctxIdx = b * kNumInteractions + i;
            buttonCtxs_[ctxIdx] = {this, ids[b], interaction};
            buttons[b]->setButtonPress(kDispatch, &buttonCtxs_[ctxIdx], interaction);
        }
    }

    controllerWirelessManager->setPeripheralCommandReceivedCallback(
        std::bind(&Controller1State::onPeripheralCommandReceived, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void Controller1State::onStateLoop(PDN* pdn) {
    if (pendingPeripheral_.hasPending) {
        pendingPeripheral_.hasPending = false;
        executePeripheralCommand(pdn,
                                 pendingPeripheral_.command,
                                 pendingPeripheral_.param1,
                                 pendingPeripheral_.param2);
    }

    // Cancel haptic blink if timer expired
    if (hapticBlinkTimer_.isRunning() && hapticBlinkTimer_.expired()) {
        pdn->getHaptics()->setIntensity(0);
        hapticBlinkTimer_.invalidate();
    }

    // Cancel LED blink if timer expired
    if (ledBlinkTimer_.isRunning() && ledBlinkTimer_.expired()) {
        pdn->getLightManager()->stopAnimation();
        pdn->getLightManager()->clear();
        ledBlinkTimer_.invalidate();
    }
}

void Controller1State::executePeripheralCommand(PDN* pdn,
                                                PeripheralCmd command,
                                                uint8_t param1,
                                                uint8_t param2) {
    switch (command) {
        case PeripheralCmd::DISPLAY_GLYPH: {
            const char* glyph = PeripheralGlyphs::glyphForId(param1);
            if (!glyph) {
                Symbol sym;
                sym.setSymbolId(static_cast<SymbolId>(param1));
                glyph = sym.getSymbolGlyph();
            }
            pdn->getDisplay()
                ->invalidateScreen()
                ->setGlyphMode(FontMode::SYMBOL_GLYPH)
                ->renderGlyph(glyph, 48, 48)
                ->render();
            break;
        }

        case PeripheralCmd::BLINK_HAPTIC: {
            // param1:param2 encode duration in 100 ms units
            uint16_t durationMs = (static_cast<uint16_t>(param1) << 8 | param2) * 100U;
            if (durationMs == 0) durationMs = 200;
            pdn->getHaptics()->setIntensity(255);
            hapticBlinkTimer_.setTimer(durationMs);
            break;
        }

        case PeripheralCmd::BLINK_LED: {
            uint16_t durationMs = (static_cast<uint16_t>(param1) << 8 | param2) * 100U;
            if (durationMs == 0) durationMs = 200;
            LOG_W(TAG, "BLINK_LED for %ums", durationMs);
            AnimationConfig cfg{};
            cfg.loop = true;
            cfg.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
            pdn->getLightManager()->startAnimation(new TransmitOnAnimation(), cfg);
            ledBlinkTimer_.setTimer(durationMs);
            break;
        }

        case PeripheralCmd::DISPLAY_ANIMATION: {
            LOG_W(TAG, "DISPLAY_ANIMATION id=%u", param1);
            AnimationConfig cfg{};
            cfg.loop = true;
            cfg.speed = 20;
            cfg.loopDelayMs = 500;

            if (param1 == 0) {
                // IdleAnimation — rainbow flow across all LEDs
                LEDColor rainbow[9] = {
                    {255, 0,   0},   // red
                    {255, 128, 0},   // orange
                    {255, 255, 0},   // yellow
                    {0,   255, 0},   // green
                    {0,   255, 255}, // cyan
                    {0,   0,   255}, // blue
                    {128, 0,   255}, // purple
                    {255, 0,   128}, // pink
                    {255, 255, 255}, // white
                };
                for (int i = 0; i < 9; i++) {
                    cfg.initialState.leftLights[i]  = LEDState::SingleLEDState(rainbow[i], 200);
                    cfg.initialState.rightLights[i] = LEDState::SingleLEDState(rainbow[i], 200);
                }
                pdn->getLightManager()->startAnimation(new IdleAnimation(), cfg);

            } else if (param1 == 1) {
                // VerticalChaseAnimation — blue/cyan chase sweeping downward
                cfg.initialState.leftLights[0] = LEDState::SingleLEDState({0,   0,   255}, 255);
                cfg.initialState.leftLights[1] = LEDState::SingleLEDState({0,   128, 255}, 200);
                cfg.initialState.leftLights[2] = LEDState::SingleLEDState({0,   200, 255}, 120);
                cfg.initialState.leftLights[3] = LEDState::SingleLEDState({0,   255, 255}, 60);
                pdn->getLightManager()->startAnimation(new VerticalChaseAnimation(), cfg);

            } else if (param1 == 2) {
                // TransmitBreathAnimation — purple breathing transmit LED
                cfg.initialState.transmitLight = LEDState::SingleLEDState({180, 0, 255}, 255);
                pdn->getLightManager()->startAnimation(new TransmitBreathAnimation(), cfg);

            } else if (param1 == 3) {
                // HunterWinAnimation — uses its own hardcoded hunter colors
                pdn->getLightManager()->startAnimation(new HunterWinAnimation(), cfg);

            } else if (param1 == 4) {
                // BountyWinAnimation — uses its own hardcoded bounty colors
                pdn->getLightManager()->startAnimation(new BountyWinAnimation(), cfg);

            } else if (param1 == static_cast<uint8_t>(QuickdrawCountdown::PeripheralAnimationId::COUNTDOWN)) {
                cfg.loop = false;
                cfg.speed = 16;
                cfg.loopDelayMs = 0;
                switch (param2) {
                    case static_cast<uint8_t>(QuickdrawCountdown::Step::THREE):
                        cfg.initialState = QuickdrawCountdown::threeLedState();
                        break;
                    case static_cast<uint8_t>(QuickdrawCountdown::Step::TWO):
                        cfg.initialState = QuickdrawCountdown::twoLedState();
                        break;
                    case static_cast<uint8_t>(QuickdrawCountdown::Step::ONE):
                        cfg.initialState = QuickdrawCountdown::oneLedState();
                        break;
                    case static_cast<uint8_t>(QuickdrawCountdown::Step::DRAW):
                        cfg.initialState = QuickdrawCountdown::drawLedState();
                        break;
                    default:
                        cfg.initialState = QuickdrawCountdown::threeLedState();
                        break;
                }
                pdn->getLightManager()->startAnimation(new CountdownAnimation(), cfg);

            } else if (param1 == static_cast<uint8_t>(QuickdrawCountdown::PeripheralAnimationId::LOSE)) {
                cfg.loop = true;
                cfg.speed = 16;
                cfg.loopDelayMs = 0;
                cfg.initialState = LEDState();
                pdn->getLightManager()->startAnimation(new LoseAnimation(), cfg);

            } else {
                LOG_W(TAG, "Unknown animation id=%u", param1);
            }
            break;
        }

        case PeripheralCmd::CLEAR_LEDS: {
            pdn->getLightManager()->stopAnimation();
            pdn->getLightManager()->clear();
            break;
        }

        case PeripheralCmd::CLEAR_DISPLAY: {
            pdn->getDisplay()->invalidateScreen()->render();
            break;
        }

        default:
            LOG_E(TAG, "Unknown peripheral command %d", static_cast<int>(command));
            break;
    }
}

void Controller1State::onStateDismounted(PDN* pdn) {
    LOG_W(TAG, "Dismounted");
    controllerWirelessManager->clearPeripheralCallback();
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();

    SimpleTimer bufferTimer;
    const int bufferTimeout = 1000;
    bufferTimer.setTimer(bufferTimeout);
    while (!bufferTimer.expired()) {
        if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            renderLoadingScreen(pdn->getDisplay());
        }
    }
}

void Controller1State::sendButtonMessage(ButtonIdentifier buttonId, ButtonInteraction interaction) {
    controllerWirelessManager->sendControllerCommandPacket(ControllerCmd::INTERACTION_REQUEST, buttonId, interaction, SerialIdentifier::OUTPUT_JACK);
}

void Controller1State::onPeripheralCommandReceived(PeripheralCmd command,
                                                   uint8_t param1,
                                                   uint8_t param2) {
    LOG_W(TAG, "Peripheral command %d (p1=%u p2=%u)", static_cast<int>(command), param1, param2);

    // Stash for use in onStateLoop (PDN device pointer is available there).
    pendingPeripheral_.hasPending = true;
    pendingPeripheral_.command    = command;
    pendingPeripheral_.param1     = param1;
    pendingPeripheral_.param2     = param2;
}