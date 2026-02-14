#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "BreachDefenseLose";

BreachDefenseLose::BreachDefenseLose(BreachDefense* game) : State(BREACH_LOSE) {
    this->game = game;
}

BreachDefenseLose::~BreachDefenseLose() {
    game = nullptr;
}

void BreachDefenseLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "BREACH OPEN — score %d", session.score);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("BREACH OPEN", 10, 30);
    PDN->getDisplay()->render();

    PDN->getHaptics()->setIntensity(255);

    loseTimer.setTimer(LOSE_DISPLAY_MS);
}

void BreachDefenseLose::onStateLoop(Device* PDN) {
    if (loseTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void BreachDefenseLose::onStateDismounted(Device* PDN) {
    loseTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool BreachDefenseLose::transitionToIntro() {
    return transitionToIntroState;
}

bool BreachDefenseLose::isTerminalState() const {
    return game->getConfig().managedMode;
}
