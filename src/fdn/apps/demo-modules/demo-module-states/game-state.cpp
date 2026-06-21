#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "GameState";
static const char* kStateLabel = "GAME";
}

GameState::GameState() : TypedState<FDN>(DemoModuleStateId::GAME) {}

GameState::~GameState() {}

void GameState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    transitionTimer.setTimer(kDemoStateDisplayMs);
    renderDemoStateLabel(fdn, kStateLabel);
}

void GameState::onStateLoop(FDN* fdn) {
    renderDemoStateLabel(fdn, kStateLabel);
}

void GameState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    transitionTimer.invalidate();
}

bool GameState::transitionToScoring() {
    return transitionTimer.expired();
}
