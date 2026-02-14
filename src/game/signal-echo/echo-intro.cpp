#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

EchoIntro::EchoIntro(SignalEcho* game) : State(ECHO_INTRO) {
    this->game = game;
}

EchoIntro::~EchoIntro() {
    game = nullptr;
}

void EchoIntro::onStateMounted(Device* PDN) {
    transitionToShowSequenceState = false;

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();
    game->setStartTime(SimpleTimer::getPlatformClock()->milliseconds());

    // Seed RNG and generate the first sequence
    game->seedRng();
    game->getSession().currentSequence = game->generateSequence(
        game->getConfig().sequenceLength);

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("SIGNAL ECHO", 15, 20)
        ->drawText("Watch. Repeat.", 10, 45);
    PDN->getDisplay()->render();

    // Start idle LED animation
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = SIGNAL_ECHO_IDLE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Start intro timer
    introTimer.setTimer(INTRO_DURATION_MS);
}

void EchoIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToShowSequenceState = true;
    }
}

void EchoIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToShowSequenceState = false;
}

bool EchoIntro::transitionToShowSequence() {
    return transitionToShowSequenceState;
}
