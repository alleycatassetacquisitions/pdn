#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "GhostRunnerWin";

GhostRunnerWin::GhostRunnerWin(GhostRunner* game) : State(GHOST_WIN) {
    this->game = game;
}

GhostRunnerWin::~GhostRunnerWin() {
    game = nullptr;
}

void GhostRunnerWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    // Stub: always easy-mode win (hardMode = false)
    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = 100;
    winOutcome.hardMode = false;
    game->setOutcome(winOutcome);

    LOG_I(TAG, "RUN COMPLETE");

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("RUN COMPLETE", 10, 30);
    PDN->getDisplay()->render();

    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void GhostRunnerWin::onStateLoop(Device* PDN) {
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

void GhostRunnerWin::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool GhostRunnerWin::transitionToIntro() {
    return transitionToIntroState;
}

bool GhostRunnerWin::isTerminalState() {
    return game->getConfig().managedMode;
}
