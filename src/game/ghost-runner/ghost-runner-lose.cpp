#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
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

    auto& session = game->getSession();

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "GHOST CAUGHT — score=%d, strikes=%d", session.score, session.strikes);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("GHOST CAUGHT", 10, 30);
    PDN->getDisplay()->render();

    // Red fade LED
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::IDLE;
    ledConfig.speed = 8;
    ledConfig.curve = EaseCurve::LINEAR;
    ledConfig.initialState = GHOST_RUNNER_LOSE_STATE;
    ledConfig.loopDelayMs = 0;
    ledConfig.loop = true;
    PDN->getLightManager()->startAnimation(ledConfig);

    // Long haptic buzz
    PDN->getHaptics()->max();

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

bool GhostRunnerLose::isTerminalState() const {
    return game->getConfig().managedMode;
}
