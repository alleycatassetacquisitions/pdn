#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SpikeVectorWin";

SpikeVectorWin::SpikeVectorWin(SpikeVector* game) : State(SPIKE_WIN) {
    this->game = game;
}

SpikeVectorWin::~SpikeVectorWin() {
    game = nullptr;
}

void SpikeVectorWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    // Stub: always easy-mode win (hardMode = false)
    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = 100;
    winOutcome.hardMode = false;
    game->setOutcome(winOutcome);

    LOG_I(TAG, "VECTOR CLEAR");

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("VECTOR CLEAR", 10, 30);
    PDN->getDisplay()->render();

    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void SpikeVectorWin::onStateLoop(Device* PDN) {
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

void SpikeVectorWin::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool SpikeVectorWin::transitionToIntro() {
    return transitionToIntroState;
}

bool SpikeVectorWin::isTerminalState() {
    return game->getConfig().managedMode;
}
