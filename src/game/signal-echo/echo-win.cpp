#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

EchoWin::EchoWin(SignalEcho* game) : State(ECHO_WIN) {
    this->game = game;
}

EchoWin::~EchoWin() {
    game = nullptr;
}

void EchoWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    // Determine if this was a hard mode win
    bool isHard = (game->getConfig().allowedMistakes <= 1 &&
                   game->getConfig().sequenceLength >= 8);

    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = session.score;
    winOutcome.hardMode = isHard;
    game->setOutcome(winOutcome);

    // Display victory screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("ACCESS GRANTED", 5, 25);

    std::string scoreStr = "Score: " + std::to_string(session.score);
    PDN->getDisplay()->drawText(scoreStr.c_str(), 30, 50);
    PDN->getDisplay()->render();

    // Rainbow LED sweep
    AnimationConfig config;
    config.type = AnimationType::VERTICAL_CHASE;
    config.speed = 5;
    config.curve = EaseCurve::EASE_IN_OUT;
    config.initialState = SIGNAL_ECHO_WIN_STATE;
    config.loopDelayMs = 500;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Celebration haptic
    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void EchoWin::onStateLoop(Device* PDN) {
    if (winTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            // In standalone mode, restart the game
            transitionToIntroState = true;
        } else {
            // In managed mode, return to the previous app (e.g. Quickdraw)
            PDN->returnToPreviousApp();
        }
    }
}

void EchoWin::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool EchoWin::transitionToIntro() {
    return transitionToIntroState;
}

bool EchoWin::isTerminalState() const {
    return game->getConfig().managedMode;
}
