#include "apps/quickdraw-demo/quickdraw-demo-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "GameState";
static const char* kStateLabel = "GAME";
}

GameState::GameState() : TypedState<FDN>(QuickdrawDemoStateId::GAME) {}

GameState::~GameState() {}

void GameState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    renderDemoStateLabel(fdn, kStateLabel);
}

void GameState::onStateLoop(FDN* fdn) {
    renderDemoStateLabel(fdn, kStateLabel);
}

void GameState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
}

bool GameState::transitionToScoring() {
    return false;
}
