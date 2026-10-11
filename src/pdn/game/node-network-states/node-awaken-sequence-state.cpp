#include "game/node-network-states.hpp"
#include "game/node-network-resources.hpp"
#include "device/device.hpp"

NodeAwakenSequence::NodeAwakenSequence(Player* player) : TypedState<PDN>(NodeNetworkStateId::AWAKEN_SEQUENCE) {
    this->player = player;
}

NodeAwakenSequence::~NodeAwakenSequence() {
    player = nullptr;
}

void NodeAwakenSequence::onStateMounted(PDN* pdn) {
    activationSequenceTimer.setTimer(activationStepDuration);
    activateMotor = true;
    pdn->getHaptics()->setIntensity(125);

    pdn->getDisplay()->
        invalidateScreen()->
        drawImage(getNodeImage(ImageType::LOGO_LEFT))->
        drawImage(getNodeImage(ImageType::STAMP))->
        render();
}

void NodeAwakenSequence::onStateLoop(PDN* pdn) {
    if (activationSequenceTimer.expired()) {
        if (activateMotorCount <= AWAKEN_THRESHOLD) {
            if (activateMotor) {
                pdn->getHaptics()->setIntensity(VIBRATION_MAX);
            } else {
                pdn->getHaptics()->setIntensity(VIBRATION_OFF);
            }
            activationSequenceTimer.setTimer(activationStepDuration);
            activateMotorCount++;
            activateMotor = !activateMotor;
        }
    }
}

void NodeAwakenSequence::onStateDismounted(PDN* pdn) {
    activationSequenceTimer.invalidate();
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    activateMotorCount = 0;
    activateMotor = false;
}

bool NodeAwakenSequence::transitionToIdle() {
    return activateMotorCount > AWAKEN_THRESHOLD;
}
