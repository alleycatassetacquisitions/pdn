#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "MainMenuState";
static const char* kStateLabel = "MAIN MENU";
}

MainMenuState::MainMenuState() : TypedState<FDN>(DemoModuleStateId::MAIN_MENU) {}

MainMenuState::~MainMenuState() {}

void MainMenuState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    transitionTimer.setTimer(kDemoStateDisplayMs);
    renderDemoStateLabel(fdn, kStateLabel);
}

void MainMenuState::onStateLoop(FDN* fdn) {
    renderDemoStateLabel(fdn, kStateLabel);
}

void MainMenuState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    transitionTimer.invalidate();
    goToTutorialNext = !goToTutorialNext;
}

bool MainMenuState::transitionToTutorial() {
    return transitionTimer.expired() && goToTutorialNext;
}

bool MainMenuState::transitionToGame() {
    return transitionTimer.expired() && !goToTutorialNext;
}
