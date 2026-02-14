#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "SpikeVectorLose";

SpikeVectorLose::SpikeVectorLose(SpikeVector* game) : State(SPIKE_LOSE) {
    this->game = game;
}

SpikeVectorLose::~SpikeVectorLose() {
    game = nullptr;
}

void SpikeVectorLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "SPIKE IMPACT — Score: %d", session.score);

    // Display defeat screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("SPIKE IMPACT", 10, 25);

    std::string scoreStr = "Score: " + std::to_string(session.score);
    PDN->getDisplay()->drawText(scoreStr.c_str(), 30, 50);
    PDN->getDisplay()->render();

    // Lose LED animation
    AnimationConfig animConfig;
    animConfig.type = AnimationType::LOSE;
    animConfig.speed = 3;
    animConfig.curve = EaseCurve::LINEAR;
    animConfig.initialState = SPIKE_VECTOR_LOSE_STATE;
    animConfig.loopDelayMs = 0;
    animConfig.loop = false;
    PDN->getLightManager()->startAnimation(animConfig);

    // Heavy haptic buzz
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

bool SpikeVectorLose::isTerminalState() const {
    return game->getConfig().managedMode;
}
