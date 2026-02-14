#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

EchoLose::EchoLose(SignalEcho* game) : State(ECHO_LOSE) {
    this->game = game;
}

EchoLose::~EchoLose() {
    game = nullptr;
}

void EchoLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    // Display defeat screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("SIGNAL LOST", 20, 30);
    PDN->getDisplay()->render();

    // Red fade LED
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 8;
    config.curve = EaseCurve::LINEAR;
    config.initialState = SIGNAL_ECHO_LOSE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Long haptic buzz
    PDN->getHaptics()->max();

    loseTimer.setTimer(LOSE_DISPLAY_MS);
}

void EchoLose::onStateLoop(Device* PDN) {
    if (loseTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            // In standalone mode, restart the game
            transitionToIntroState = true;
        } else {
            // In managed mode, return to the previous app
            PDN->returnToPreviousApp();
        }
    }
}

void EchoLose::onStateDismounted(Device* PDN) {
    loseTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool EchoLose::transitionToIntro() {
    return transitionToIntroState;
}

bool EchoLose::isTerminalState() const {
    return game->getConfig().managedMode;
}
