#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "GhostRunnerIntro";

GhostRunnerIntro::GhostRunnerIntro(GhostRunner* game) : State(GHOST_INTRO) {
    this->game = game;
}

GhostRunnerIntro::~GhostRunnerIntro() {
    game = nullptr;
}

void GhostRunnerIntro::onStateMounted(Device* PDN) {
    transitionToShowState = false;

    LOG_I(TAG, "Ghost Runner intro");

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();
    game->setStartTime(SimpleTimer::getPlatformClock()->milliseconds());

    // Seed RNG for this run
    game->seedRng();

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("GHOST RUNNER", 10, 20)
        ->drawText("Phase through.", 10, 45);
    PDN->getDisplay()->render();

    // Start idle LED animation
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = GHOST_RUNNER_IDLE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Start intro timer
    introTimer.setTimer(INTRO_DURATION_MS);
}

void GhostRunnerIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToShowState = true;
    }
}

void GhostRunnerIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToShowState = false;
}

bool GhostRunnerIntro::transitionToShow() {
    return transitionToShowState;
}
