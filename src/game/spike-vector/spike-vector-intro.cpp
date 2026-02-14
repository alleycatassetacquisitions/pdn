#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SpikeVectorIntro";

SpikeVectorIntro::SpikeVectorIntro(SpikeVector* game) : State(SPIKE_INTRO) {
    this->game = game;
}

SpikeVectorIntro::~SpikeVectorIntro() {
    game = nullptr;
}

void SpikeVectorIntro::onStateMounted(Device* PDN) {
    transitionToShowState = false;

    LOG_I(TAG, "Spike Vector intro");

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();
    game->setStartTime(SimpleTimer::getPlatformClock()->milliseconds());
    game->seedRng();

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("SPIKE VECTOR", 10, 20)
        ->drawText("Dodge the grid.", 10, 45);
    PDN->getDisplay()->render();

    // Start idle LED animation
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = SPIKE_VECTOR_IDLE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Start intro timer
    introTimer.setTimer(INTRO_DURATION_MS);
}

void SpikeVectorIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToShowState = true;
    }
}

void SpikeVectorIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToShowState = false;
}

bool SpikeVectorIntro::transitionToShow() {
    return transitionToShowState;
}
