#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SpikeVectorIntro";

SpikeVectorIntro::SpikeVectorIntro(SpikeVector* game) : State(SPIKE_INTRO) {
    this->game = game;
}

SpikeVectorIntro::~SpikeVectorIntro() {
    game = nullptr;
}

void SpikeVectorIntro::onStateMounted(Device* PDN) {
    transitionToWinState = false;

    LOG_I(TAG, "Spike Vector intro");

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("SPIKE VECTOR", 10, 20)
        ->drawText("Dodge the grid.", 10, 45);
    PDN->getDisplay()->render();

    // Start intro timer — stub auto-wins after this
    introTimer.setTimer(INTRO_DURATION_MS);
}

void SpikeVectorIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToWinState = true;
    }
}

void SpikeVectorIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToWinState = false;
}

bool SpikeVectorIntro::transitionToWin() {
    return transitionToWinState;
}
