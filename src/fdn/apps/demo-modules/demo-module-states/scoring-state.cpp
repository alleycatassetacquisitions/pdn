#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "ScoringState";
}

ScoringState::ScoringState() : TypedState<FDN>(DemoModuleStateId::SCORING) {}

ScoringState::~ScoringState() {}

void ScoringState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
}

void ScoringState::onStateLoop(FDN* fdn) {
    LOG_W(TAG, "Loop");
}

void ScoringState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
}

bool ScoringState::transitionToMainMenu() {
    return true;
}
