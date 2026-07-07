#include "apps/crypt-creeper/crypt-creeper-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "ScoringState";
static const char* kStateLabel = "SCORING";
}

ScoringState::ScoringState() : TypedState<FDN>(CryptCreeperStateId::SCORING) {}

ScoringState::~ScoringState() {}

void ScoringState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    renderDemoStateLabel(fdn, kStateLabel);
}

void ScoringState::onStateLoop(FDN* fdn) {
    renderDemoStateLabel(fdn, kStateLabel);
}

void ScoringState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
}

bool ScoringState::transitionToMainMenu() {
    return false;
}
