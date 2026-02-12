#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "GhostRunnerLose";

GhostRunnerLose::GhostRunnerLose(GhostRunner* game) : State(GHOST_LOSE) {
    this->game = game;
}

GhostRunnerLose::~GhostRunnerLose() {
    game = nullptr;
}

void GhostRunnerLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = 0;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "GHOST CAUGHT");

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("GHOST CAUGHT", 10, 30);
    PDN->getDisplay()->render();

    PDN->getHaptics()->setIntensity(255);

    loseTimer.setTimer(LOSE_DISPLAY_MS);
}

void GhostRunnerLose::onStateLoop(Device* PDN) {
    if (loseTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void GhostRunnerLose::onStateDismounted(Device* PDN) {
    loseTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool GhostRunnerLose::transitionToIntro() {
    return transitionToIntroState;
}

bool GhostRunnerLose::isTerminalState() {
    return game->getConfig().managedMode;
}
