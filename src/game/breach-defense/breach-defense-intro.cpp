#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "BreachDefenseIntro";

BreachDefenseIntro::BreachDefenseIntro(BreachDefense* game) : State(BREACH_INTRO) {
    this->game = game;
}

BreachDefenseIntro::~BreachDefenseIntro() {
    game = nullptr;
}

void BreachDefenseIntro::onStateMounted(Device* PDN) {
    transitionToWinState = false;

    LOG_I(TAG, "Breach Defense intro");

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("BREACH DEFENSE", 10, 20)
        ->drawText("Hold the line.", 10, 45);
    PDN->getDisplay()->render();

    // Start intro timer — stub auto-wins after this
    introTimer.setTimer(INTRO_DURATION_MS);
}

void BreachDefenseIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToWinState = true;
    }
}

void BreachDefenseIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToWinState = false;
}

bool BreachDefenseIntro::transitionToWin() {
    return transitionToWinState;
}
