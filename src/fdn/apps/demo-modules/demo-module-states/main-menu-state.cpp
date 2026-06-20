#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "MainMenuState";
}

MainMenuState::MainMenuState() : TypedState<FDN>(DemoModuleStateId::MAIN_MENU) {}

MainMenuState::~MainMenuState() {}

void MainMenuState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
}

void MainMenuState::onStateLoop(FDN* fdn) {
    LOG_W(TAG, "Loop");
}

void MainMenuState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
}

bool MainMenuState::transitionToTutorial() {
    return true;
}

bool MainMenuState::transitionToGame() {
    return true;
}
