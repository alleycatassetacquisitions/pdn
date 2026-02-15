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

    // Display round info with visual preview
    PDN->getDisplay()->invalidateScreen();

    std::string roundStr = "R" + std::to_string(session.currentRound + 1) +
                           "/" + std::to_string(config.rounds);
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(roundStr.c_str(), 2, 2);

    // Show score
    std::string scoreStr = "Score: " + std::to_string(session.score);
    PDN->getDisplay()->drawText(scoreStr.c_str(), 60, 2);

    // Draw preview lanes
    PDN->getDisplay()->setDrawColor(1)
        ->drawFrame(0, config.UP_LANE_Y, 128, config.UP_LANE_HEIGHT);
    PDN->getDisplay()->drawFrame(config.HIT_ZONE_X, config.UP_LANE_Y + 2,
                                  config.hitZoneWidthPx, config.UP_LANE_HEIGHT - 4);

    PDN->getDisplay()->drawBox(0, config.DIVIDER_Y, 128, 2);

    PDN->getDisplay()->drawFrame(0, config.DOWN_LANE_Y, 128, config.DOWN_LANE_HEIGHT);
    PDN->getDisplay()->drawFrame(config.HIT_ZONE_X, config.DOWN_LANE_Y + 2,
                                  config.hitZoneWidthPx, config.DOWN_LANE_HEIGHT - 4);

    // Draw sample arrow in hit zone (tutorial hint)
    int arrowX = config.HIT_ZONE_X + 6;
    int arrowYUp = config.UP_LANE_Y + config.UP_LANE_HEIGHT / 2 - 4;
    PDN->getDisplay()->drawBox(arrowX, arrowYUp, 8, 8);

    // Show lives
    for (int i = 0; i < config.lives; i++) {
        int lx = 2 + i * 8;
        if (i < session.livesRemaining) {
            PDN->getDisplay()->setDrawColor(1)->drawBox(lx, 56, 5, 5);
        } else {
            PDN->getDisplay()->setDrawColor(1)->drawFrame(lx, 56, 5, 5);
        }
    }

    // Controls hint
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("GET READY!", 40, 56);

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
