#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "GhostRunnerShow";

GhostRunnerShow::GhostRunnerShow(GhostRunner* game) : State(GHOST_SHOW) {
    this->game = game;
}

GhostRunnerShow::~GhostRunnerShow() {
    game = nullptr;
}

void GhostRunnerShow::onStateMounted(Device* PDN) {
    transitionToGameplayState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    LOG_I(TAG, "Round %d of %d", session.currentRound + 1, config.rounds);

    // Display round info
    PDN->getDisplay()->invalidateScreen();

    std::string roundStr = "Round " + std::to_string(session.currentRound + 1) +
                           " of " + std::to_string(config.rounds);
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("GET READY", 25, 20)
        ->drawText(roundStr.c_str(), 15, 40);

    // Show strikes remaining
    int livesRemaining = config.missesAllowed - session.strikes;
    if (livesRemaining < 0) livesRemaining = 0;

    std::string lives;
    for (int i = 0; i < config.missesAllowed; i++) {
        if (i < livesRemaining) {
            lives += "* ";
        } else {
            lives += "o ";
        }
    }
    PDN->getDisplay()->drawText(lives.c_str(), 15, 58);
    PDN->getDisplay()->render();

    // Light pulse haptic feedback
    PDN->getHaptics()->setIntensity(128);

    showTimer.setTimer(SHOW_DURATION_MS);
}

void GhostRunnerShow::onStateLoop(Device* PDN) {
    if (showTimer.expired()) {
        PDN->getHaptics()->off();
        transitionToGameplayState = true;
    }
}

void GhostRunnerShow::onStateDismounted(Device* PDN) {
    showTimer.invalidate();
    transitionToGameplayState = false;
    PDN->getHaptics()->off();
}

bool GhostRunnerShow::transitionToGameplay() {
    return transitionToGameplayState;
}
