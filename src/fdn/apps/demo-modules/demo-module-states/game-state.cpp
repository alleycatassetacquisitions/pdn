#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "GameState";
}

GameState::GameState() : TypedState<FDN>(DemoModuleStateId::GAME) {}

GameState::~GameState() {}

void GameState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
}

void GameState::onStateLoop(FDN* fdn) {
    LOG_W(TAG, "Loop");
}

void GameState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
}

bool GameState::transitionToScoring() {
    return true;
}
