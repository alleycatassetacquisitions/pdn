#include "apps/controller/controller-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "Controller1State";
}

Controller1State::Controller1State() : TypedState<PDN>(ControllerStateId::CONTROLLER1) {}

Controller1State::~Controller1State() {}

void Controller1State::onStateMounted(PDN* pdn) {
    LOG_W(TAG, "Mounted");
}

void Controller1State::onStateLoop(PDN* pdn) {
    LOG_W(TAG, "Loop");
}

void Controller1State::onStateDismounted(PDN* pdn) {
    LOG_W(TAG, "Dismounted");
}