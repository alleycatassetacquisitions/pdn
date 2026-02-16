#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

EchoEvaluate::EchoEvaluate(SignalEcho* game) : State(ECHO_EVALUATE) {
    this->game = game;
}

EchoEvaluate::~EchoEvaluate() {
    game = nullptr;
}

void EchoEvaluate::onStateMounted(Device* PDN) {
    transitionToShowSequenceState = false;
    transitionToWinState = false;
    transitionToLoseState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Full-sequence binary comparison
    bool sequenceMatches = true;
    int seqLen = static_cast<int>(session.currentSequence.size());

    if (static_cast<int>(session.playerInput.size()) != seqLen) {
        sequenceMatches = false;
    } else {
        for (int i = 0; i < seqLen; i++) {
            if (session.playerInput[i] != session.currentSequence[i]) {
                sequenceMatches = false;
                break;
            }
        }
    }

    if (!sequenceMatches) {
        // Wrong sequence — game over
        transitionToLoseState = true;
        return;
    }

    // Correct sequence — award score
    session.score += seqLen * 100;

    // Check if all rounds completed
    session.currentRound++;
    if (session.currentRound >= config.numSequences) {
        transitionToWinState = true;
        return;
    }

    // Round clear celebration
    PDN->getDisplay()->invalidateScreen();
    SignalEchoRenderer::drawHUD(PDN->getDisplay(), session.currentRound - 1, config.numSequences);
    SignalEchoRenderer::drawSeparators(PDN->getDisplay());

    // Show "SEQ N LOCKED" message
    std::string lockMsg = "SEQ " + std::to_string(session.currentRound) + " LOCKED";
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(lockMsg.c_str(), 25, 30);
    PDN->getDisplay()->render();

    // Celebration haptic
    PDN->getHaptics()->setIntensity(200);

    // Green LED chase
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::VERTICAL_CHASE;
    ledConfig.speed = 5;
    ledConfig.curve = EaseCurve::EASE_IN_OUT;
    ledConfig.loop = false;

    LEDState state;
    LEDColor green[4] = {
        LEDColor(0, 255, 0),
        LEDColor(0, 220, 0),
        LEDColor(0, 180, 0),
        LEDColor(0, 140, 0)
    };
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(green[i % 4], 200);
        state.rightLights[i] = LEDState::SingleLEDState(green[i % 4], 200);
    }
    ledConfig.initialState = state;
    PDN->getLightManager()->startAnimation(ledConfig);

    // Brief delay before next round (non-blocking)
    // Note: LED animation and haptic will auto-cleanup on state dismount

    // Generate next sequence
    if (config.cumulative) {
        session.currentSequence.push_back(rand() % 2 == 0);
    } else {
        session.currentSequence = game->generateSequence(config.sequenceLength);
    }

    // Reset input for next round
    session.inputIndex = 0;
    session.playerInput.clear();

    transitionToShowSequenceState = true;
}

void EchoEvaluate::onStateLoop(Device* PDN) {
    // All logic handled in onStateMounted
}

void EchoEvaluate::onStateDismounted(Device* PDN) {
    transitionToShowSequenceState = false;
    transitionToWinState = false;
    transitionToLoseState = false;
}

bool EchoEvaluate::transitionToShowSequence() {
    return transitionToShowSequenceState;
}

bool EchoEvaluate::transitionToWin() {
    return transitionToWinState;
}

bool EchoEvaluate::transitionToLose() {
    return transitionToLoseState;
}
