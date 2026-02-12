#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SpikeVectorLose";

SpikeVectorLose::SpikeVectorLose(SpikeVector* game) : State(SPIKE_LOSE) {
    this->game = game;
}

SpikeVectorLose::~SpikeVectorLose() {
    game = nullptr;
}

void SpikeVectorLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = 0;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "SPIKE IMPACT");

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("SPIKE IMPACT", 10, 30);
    PDN->getDisplay()->render();

    PDN->getHaptics()->setIntensity(255);

    loseTimer.setTimer(LOSE_DISPLAY_MS);
}

void SpikeVectorLose::onStateLoop(Device* PDN) {
    if (loseTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void SpikeVectorLose::onStateDismounted(Device* PDN) {
    loseTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool SpikeVectorLose::transitionToIntro() {
    return transitionToIntroState;
}

bool SpikeVectorLose::isTerminalState() {
    return game->getConfig().managedMode;
}
