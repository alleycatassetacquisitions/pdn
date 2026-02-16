#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

EchoPlayerInput::EchoPlayerInput(SignalEcho* game) : State(ECHO_PLAYER_INPUT) {
    this->game = game;
}

EchoPlayerInput::~EchoPlayerInput() {
    game = nullptr;
}

void EchoPlayerInput::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;
    displayIsDirty = true;

    // Set up button callbacks
    parameterizedCallbackFunction upCallback = [](void* ctx) {
        auto* state = static_cast<EchoPlayerInput*>(ctx);
        state->handleInput(true, nullptr);
    };

    parameterizedCallbackFunction downCallback = [](void* ctx) {
        auto* state = static_cast<EchoPlayerInput*>(ctx);
        state->handleInput(false, nullptr);
    };

    PDN->getPrimaryButton()->setButtonPress(upCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(downCallback, this, ButtonInteraction::CLICK);

    // Start idle LED animation
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::IDLE;
    ledConfig.speed = 12;
    ledConfig.curve = EaseCurve::LINEAR;
    ledConfig.loop = true;

    LEDState state;
    LEDColor blue[4] = {
        LEDColor(0, 100, 200),
        LEDColor(0, 120, 220),
        LEDColor(0, 140, 240),
        LEDColor(0, 160, 255)
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(blue[i % 4], 120);
        state.rightLights[i] = LEDState::SingleLEDState(blue[i % 4], 120);
    }
    ledConfig.initialState = state;
    PDN->getLightManager()->startAnimation(ledConfig);

    renderInputScreen(PDN);
}

void EchoPlayerInput::onStateLoop(Device* PDN) {
    if (displayIsDirty) {
        renderInputScreen(PDN);
        displayIsDirty = false;
    }

    auto& session = game->getSession();
    int seqLen = static_cast<int>(session.currentSequence.size());

    // Check if all inputs have been entered
    if (session.inputIndex >= seqLen) {
        transitionToEvaluateState = true;
        return;
    }
}

void EchoPlayerInput::onStateDismounted(Device* PDN) {
    transitionToEvaluateState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getHaptics()->off();
}

bool EchoPlayerInput::transitionToEvaluate() {
    return transitionToEvaluateState;
}

void EchoPlayerInput::handleInput(bool isUp, Device* PDN) {
    auto& session = game->getSession();
    int seqLen = static_cast<int>(session.currentSequence.size());

    if (session.inputIndex >= seqLen) {
        return;  // Already completed input for this round
    }

    // Store input (no immediate evaluation)
    session.playerInput.push_back(isUp);
    session.inputIndex++;

    // Light haptic feedback
    if (PDN != nullptr) {
        PDN->getHaptics()->setIntensity(100);
        // Haptic will auto-off in next render cycle
    }

    displayIsDirty = true;
}

void EchoPlayerInput::renderInputScreen(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();
    int seqLen = static_cast<int>(session.currentSequence.size());

    PDN->getDisplay()->invalidateScreen();

    // HUD
    SignalEchoRenderer::drawHUD(PDN->getDisplay(), session.currentRound, config.numSequences);
    SignalEchoRenderer::drawSeparators(PDN->getDisplay());

    // Label
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("RECALL", 48, 16);

    // Slots with player input and active cursor
    auto layout = SignalEchoRenderer::getLayout(seqLen);
    int activeSlot = session.inputIndex < seqLen ? session.inputIndex : -1;

    // Draw all slots
    for (int i = 0; i < layout.count; i++) {
        int x = layout.startX + i * (layout.slotWidth + layout.gap);
        int y = layout.startY;

        if (i == activeSlot) {
            // Active cursor — inverted white box
            SignalEchoRenderer::drawSlotActive(PDN->getDisplay(), x, y,
                                               layout.slotWidth, layout.slotHeight);
        } else if (i < session.inputIndex) {
            // Player has input this slot
            bool isUp = session.playerInput[i];
            SignalEchoRenderer::drawSlotFilled(PDN->getDisplay(), x, y,
                                               layout.slotWidth, layout.slotHeight, isUp);
        } else {
            // Empty future slot
            SignalEchoRenderer::drawSlotEmpty(PDN->getDisplay(), x, y,
                                              layout.slotWidth, layout.slotHeight);
        }
    }

    // Controls
    SignalEchoRenderer::drawControls(PDN->getDisplay(), true);

    PDN->getDisplay()->render();

    // Turn off haptic after rendering (50ms pulse)
    PDN->getHaptics()->off();
}
