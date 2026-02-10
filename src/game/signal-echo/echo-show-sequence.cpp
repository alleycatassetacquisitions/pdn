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
    displayedCurrentSignal = false;

    // Reset input index for the new round
    game->getSession().inputIndex = 0;
}

void EchoShowSequence::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    int seqLen = static_cast<int>(session.currentSequence.size());

    if (currentSignalIndex >= seqLen) {
        // All signals shown — transition to player input
        transitionToPlayerInputState = true;
        return;
    }

    if (!displayedCurrentSignal) {
        // Display the current signal
        bool isUp = session.currentSequence[currentSignalIndex];
        displaySignal(PDN, isUp, currentSignalIndex, seqLen);

        // Light pulse haptic feedback per signal
        PDN->getHaptics()->setIntensity(128);

        // Start timer for this signal's display duration
        signalTimer.setTimer(game->getConfig().displaySpeedMs);
        displayedCurrentSignal = true;
    }

    if (signalTimer.expired()) {
        PDN->getHaptics()->off();
        currentSignalIndex++;
        displayedCurrentSignal = false;
    }
}

void EchoShowSequence::onStateDismounted(Device* PDN) {
    signalTimer.invalidate();
    transitionToPlayerInputState = false;
    PDN->getHaptics()->off();
}

bool EchoShowSequence::transitionToPlayerInput() {
    return transitionToPlayerInputState;
}

void EchoShowSequence::displaySignal(Device* PDN, bool isUp, int index, int total) {
    PDN->getDisplay()->invalidateScreen();

    if (isUp) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("^", 60, 25)
            ->drawText("UP", 55, 40);
    } else {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("v", 60, 25)
            ->drawText("DOWN", 48, 40);
    }

    // Progress indicator: "Signal N of M"
    std::string progress = "Signal " + std::to_string(index + 1) +
                           " of " + std::to_string(total);
    PDN->getDisplay()->drawText(progress.c_str(), 20, 58);
    PDN->getDisplay()->render();

    // LED chase direction matches signal direction
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::VERTICAL_CHASE;
    ledConfig.speed = 8;
    ledConfig.curve = EaseCurve::EASE_OUT;
    ledConfig.loop = false;

    LEDState state;
    for (int i = 0; i < 9; i++) {
        LEDColor color = signalEchoColors[i % 4];
        state.leftLights[i] = LEDState::SingleLEDState(color, 200);
        state.rightLights[i] = LEDState::SingleLEDState(color, 200);
    }
    ledConfig.initialState = state;
    PDN->getLightManager()->startAnimation(ledConfig);
}
