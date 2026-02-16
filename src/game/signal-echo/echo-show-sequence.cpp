#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

EchoShowSequence::EchoShowSequence(SignalEcho* game) : State(ECHO_SHOW_SEQUENCE) {
    this->game = game;
}

EchoShowSequence::~EchoShowSequence() {
    game = nullptr;
}

void EchoShowSequence::onStateMounted(Device* PDN) {
    transitionToPlayerInputState = false;
    currentSignalIndex = 0;
    phase = ShowPhase::INITIAL_PAUSE;

    // Reset input index for the new round
    game->getSession().inputIndex = 0;
    game->getSession().playerInput.clear();

    // Initial 300ms pause with all slots empty
    renderSlots(PDN);
    phaseTimer.setTimer(300);
}

void EchoShowSequence::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    int seqLen = static_cast<int>(session.currentSequence.size());

    if (!phaseTimer.expired()) {
        return;
    }

    switch (phase) {
        case ShowPhase::INITIAL_PAUSE:
            // Start revealing signals
            phase = ShowPhase::REVEALING;
            currentSignalIndex = 0;
            // Fall through to revealing
            [[fallthrough]];

        case ShowPhase::REVEALING: {
            if (currentSignalIndex >= seqLen) {
                // All signals revealed — start memorization pause
                phase = ShowPhase::MEMORIZE_PAUSE;
                phaseTimer.setTimer(800);
                return;
            }

            // Flash the new signal
            renderSlots(PDN);
            PDN->getDisplay()->setDrawColor(2);  // XOR mode
            auto layout = SignalEchoRenderer::getLayout(seqLen);
            int x = layout.startX + currentSignalIndex * (layout.slotWidth + layout.gap);
            PDN->getDisplay()->drawBox(x, layout.startY, layout.slotWidth, layout.slotHeight);
            PDN->getDisplay()->setDrawColor(1);  // back to normal
            PDN->getDisplay()->render();

            // Haptic pulse
            PDN->getHaptics()->setIntensity(180);

            // LED chase
            AnimationConfig ledConfig;
            ledConfig.type = AnimationType::VERTICAL_CHASE;
            ledConfig.speed = 8;
            ledConfig.curve = EaseCurve::EASE_OUT;
            ledConfig.loop = false;

            LEDState state;
            LEDColor cyan[4] = {
                LEDColor(0, 255, 255),
                LEDColor(0, 200, 200),
                LEDColor(0, 180, 180),
                LEDColor(0, 160, 160)
            };
            for (int i = 0; i < 9; i++) {
                state.leftLights[i] = LEDState::SingleLEDState(cyan[i % 4], 200);
                state.rightLights[i] = LEDState::SingleLEDState(cyan[i % 4], 200);
            }
            ledConfig.initialState = state;
            PDN->getLightManager()->startAnimation(ledConfig);

            // Hold for remaining display time
            phase = ShowPhase::SIGNAL_HOLD;
            phaseTimer.setTimer(game->getConfig().displaySpeedMs - 100);  // 100ms was flash
            break;
        }

        case ShowPhase::SIGNAL_HOLD:
            PDN->getHaptics()->off();
            currentSignalIndex++;
            renderSlots(PDN);  // Re-render without flash
            phase = ShowPhase::REVEALING;
            break;

        case ShowPhase::MEMORIZE_PAUSE:
            // Long rumble — "commit to memory" cue
            PDN->getHaptics()->setIntensity(200);
            phase = ShowPhase::COMMIT_RUMBLE;
            phaseTimer.setTimer(300);
            break;

        case ShowPhase::COMMIT_RUMBLE:
            PDN->getHaptics()->off();
            // Wipe all arrows clean
            phase = ShowPhase::WIPE_CLEAN;
            currentSignalIndex = 0;  // Reset for empty render
            renderSlots(PDN);
            phaseTimer.setTimer(300);
            break;

        case ShowPhase::WIPE_CLEAN:
            phase = ShowPhase::FINAL_PAUSE;
            transitionToPlayerInputState = true;
            break;

        case ShowPhase::FINAL_PAUSE:
            break;
    }
}

void EchoShowSequence::onStateDismounted(Device* PDN) {
    phaseTimer.invalidate();
    transitionToPlayerInputState = false;
    PDN->getHaptics()->off();
}

bool EchoShowSequence::transitionToPlayerInput() {
    return transitionToPlayerInputState;
}

void EchoShowSequence::renderSlots(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();
    int seqLen = static_cast<int>(session.currentSequence.size());

    PDN->getDisplay()->invalidateScreen();

    // HUD
    SignalEchoRenderer::drawHUD(PDN->getDisplay(), session.currentRound, config.numSequences);
    SignalEchoRenderer::drawSeparators(PDN->getDisplay());

    // Label
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("MEMORIZE", 38, 16);

    // Slots with cumulative reveal
    auto layout = SignalEchoRenderer::getLayout(seqLen);
    int revealCount = (phase == ShowPhase::WIPE_CLEAN || phase == ShowPhase::FINAL_PAUSE)
                      ? 0 : currentSignalIndex + 1;
    SignalEchoRenderer::drawAllSlots(PDN->getDisplay(), layout,
                                      session.currentSequence,
                                      revealCount, -1);

    // Progress bar during reveal
    if (phase == ShowPhase::REVEALING || phase == ShowPhase::SIGNAL_HOLD) {
        SignalEchoRenderer::drawProgressBar(PDN->getDisplay(), currentSignalIndex + 1, seqLen);
    }

    PDN->getDisplay()->render();
}
