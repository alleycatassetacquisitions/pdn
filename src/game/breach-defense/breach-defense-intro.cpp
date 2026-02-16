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
    transitionToGameplayState = false;

    LOG_I(TAG, "Breach Defense intro");

    // Reset session for a fresh game
    game->getSession().reset();
    game->resetGame();

    PlatformClock* clock = SimpleTimer::getPlatformClock();
    game->setStartTime(clock != nullptr ? clock->milliseconds() : 0);
    game->seedRng(game->getConfig().rngSeed);

    // Display title screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("BREACH DEFENSE", 10, 20)
        ->drawText("Hold the line.", 10, 45);
    PDN->getDisplay()->render();

    // LED idle animation
    PDN->getLightManager()->startAnimation({
        AnimationType::IDLE, true, 2, EaseCurve::EASE_IN_OUT,
        LEDState(), 0
    });

    // Start intro timer
    introTimer.setTimer(INTRO_DURATION_MS);
}

void BreachDefenseIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToGameplayState = true;
    }
}

void BreachDefenseIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToGameplayState = false;
}

bool BreachDefenseIntro::transitionToGameplay() {
    return transitionToGameplayState;
}
