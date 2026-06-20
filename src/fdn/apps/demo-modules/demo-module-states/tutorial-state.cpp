#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "TutorialState";
}

TutorialState::TutorialState() : TypedState<FDN>(DemoModuleStateId::TUTORIAL) {}

TutorialState::~TutorialState() {}

void TutorialState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
}

void TutorialState::onStateLoop(FDN* fdn) {
    LOG_W(TAG, "Loop");
}

void TutorialState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
}

bool TutorialState::transitionToMainMenu() {
    return true;
}
