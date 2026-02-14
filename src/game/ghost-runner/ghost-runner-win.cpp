#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "GhostRunnerWin";

GhostRunnerWin::GhostRunnerWin(GhostRunner* game) : State(GHOST_WIN) {
    this->game = game;
}

GhostRunnerWin::~GhostRunnerWin() {
    game = nullptr;
}

void GhostRunnerWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Determine if this was a hard mode win
    int zoneWidth = config.targetZoneEnd - config.targetZoneStart;
    bool isHard = (config.missesAllowed <= 1 && zoneWidth <= 16);

    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = session.score;
    winOutcome.hardMode = isHard;
    game->setOutcome(winOutcome);

    LOG_I(TAG, "RUN COMPLETE — score=%d, hardMode=%s",
          session.score, isHard ? "true" : "false");

    // Display victory screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("RUN COMPLETE", 10, 25);

    std::string scoreStr = "Score: " + std::to_string(session.score);
    PDN->getDisplay()->drawText(scoreStr.c_str(), 30, 50);
    PDN->getDisplay()->render();

    // Win LED animation
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::VERTICAL_CHASE;
    ledConfig.speed = 5;
    ledConfig.curve = EaseCurve::EASE_IN_OUT;
    ledConfig.initialState = GHOST_RUNNER_WIN_STATE;
    ledConfig.loopDelayMs = 500;
    ledConfig.loop = true;
    PDN->getLightManager()->startAnimation(ledConfig);

    // Celebration haptic
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

bool GhostRunnerWin::isTerminalState() const {
    return game->getConfig().managedMode;
}
