#include "apps/controller/controller-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "ControllerSelectState";
}

ControllerSelectState::ControllerSelectState() : TypedState<PDN>(ControllerStateId::CONTROLLER_SELECT) {}

ControllerSelectState::~ControllerSelectState() {}

void ControllerSelectState::onStateMounted(PDN* pdn) {
    LOG_W(TAG, "Mounted");
}

void ControllerSelectState::onStateLoop(PDN* pdn) {
    LOG_W(TAG, "Loop");
}

void ControllerSelectState::onStateDismounted(PDN* pdn) {
    LOG_W(TAG, "Dismounted");
}

bool ControllerSelectState::transitionToController1() {
    return true;
}