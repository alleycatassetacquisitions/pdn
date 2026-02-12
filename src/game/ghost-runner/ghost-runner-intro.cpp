#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "GhostRunnerIntro";

GhostRunnerIntro::GhostRunnerIntro(GhostRunner* game) : State(GHOST_INTRO) {
    this->game = game;
}

GhostRunnerIntro::~GhostRunnerIntro() {
    game = nullptr;
}

void GhostRunnerIntro::onStateMounted(Device* PDN) {
    transitionToWinState = false;

    LOG_I(TAG, "Ghost Runner intro");

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("GHOST RUNNER", 10, 20)
        ->drawText("Phase through.", 10, 45);
    PDN->getDisplay()->render();

    // Start intro timer — stub auto-wins after this
    introTimer.setTimer(INTRO_DURATION_MS);
}

void GhostRunnerIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToWinState = true;
    }
}

void GhostRunnerIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToWinState = false;
}

bool GhostRunnerIntro::transitionToWin() {
    return transitionToWinState;
}
