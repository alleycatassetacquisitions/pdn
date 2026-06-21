#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "TutorialState";
static const char* kStateLabel = "TUTORIAL";
}

TutorialState::TutorialState() : TypedState<FDN>(DemoModuleStateId::TUTORIAL) {}

TutorialState::~TutorialState() {}

void TutorialState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    transitionTimer.setTimer(kDemoStateDisplayMs);
    renderDemoStateLabel(fdn, kStateLabel);
}

void TutorialState::onStateLoop(FDN* fdn) {
    renderDemoStateLabel(fdn, kStateLabel);
}

void TutorialState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    transitionTimer.invalidate();
}

bool TutorialState::transitionToMainMenu() {
    return transitionTimer.expired();
}
