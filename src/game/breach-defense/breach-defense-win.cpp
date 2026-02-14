#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "BreachDefenseWin";

BreachDefenseWin::BreachDefenseWin(BreachDefense* game) : State(BREACH_WIN) {
    this->game = game;
}

BreachDefenseWin::~BreachDefenseWin() {
    game = nullptr;
}

void BreachDefenseWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& config = game->getConfig();
    auto& session = game->getSession();

    bool isHard = (config.numLanes >= 5 && config.missesAllowed <= 1);

    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = session.score;
    winOutcome.hardMode = isHard;
    game->setOutcome(winOutcome);

    LOG_I(TAG, "BREACH BLOCKED — score %d, hard %d", session.score, isHard);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("BREACH BLOCKED", 10, 30);
    PDN->getDisplay()->render();

    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void BreachDefenseWin::onStateLoop(Device* PDN) {
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

void BreachDefenseWin::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool BreachDefenseWin::transitionToIntro() {
    return transitionToIntroState;
}

bool BreachDefenseWin::isTerminalState() const {
    return game->getConfig().managedMode;
}
