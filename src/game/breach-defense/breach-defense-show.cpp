#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "device/drivers/logger.hpp"
#include <cstdio>
#include <cstdlib>

static const char* TAG = "BreachDefenseShow";

BreachDefenseShow::BreachDefenseShow(BreachDefense* game) : State(BREACH_SHOW) {
    this->game = game;
}

BreachDefenseShow::~BreachDefenseShow() {
    game = nullptr;
}

void BreachDefenseShow::onStateMounted(Device* PDN) {
    transitionToGameplayState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Generate random threat lane
    session.threatLane = rand() % config.numLanes;
    session.threatPosition = 0;
    session.threatArrived = false;

    LOG_I(TAG, "Threat %d of %d on lane %d",
          session.currentThreat + 1, config.totalThreats, session.threatLane);

    // Display threat info
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    char threatStr[32];
    snprintf(threatStr, sizeof(threatStr), "Threat %d of %d",
             session.currentThreat + 1, config.totalThreats);
    PDN->getDisplay()->drawText(threatStr, 10, 20);

    char breachStr[32];
    int remaining = config.missesAllowed - session.breaches;
    snprintf(breachStr, sizeof(breachStr), "Lives: %d", remaining);
    PDN->getDisplay()->drawText(breachStr, 10, 40);

    PDN->getDisplay()->render();

    // Haptic pulse
    PDN->getHaptics()->setIntensity(100);

    showTimer.setTimer(SHOW_DURATION_MS);
}

void BreachDefenseShow::onStateLoop(Device* PDN) {
    if (showTimer.expired()) {
        PDN->getHaptics()->off();
        transitionToGameplayState = true;
    }
}

void BreachDefenseShow::onStateDismounted(Device* PDN) {
    showTimer.invalidate();
    transitionToGameplayState = false;
    PDN->getHaptics()->off();
}

bool BreachDefenseShow::transitionToGameplay() {
    return transitionToGameplayState;
}
