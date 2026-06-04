#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"

AwakenSequence::AwakenSequence(Player* player) : TypedState<PDN>(AWAKEN_SEQUENCE) {
    this->player = player;
}

AwakenSequence::~AwakenSequence() {
    player = nullptr;
}

void AwakenSequence::onStateMounted(PDN* pdn) {
    activationSequenceTimer.setTimer(activationStepDuration);
    activateMotor= true;
    pdn->getHaptics()->setIntensity(125);

    pdn->getDisplay()->
    invalidateScreen()->
        drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_LEFT))->
        drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::STAMP))->
        render();
}

void AwakenSequence::onStateLoop(PDN* pdn) {
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

void AwakenSequence::onStateDismounted(PDN* pdn) {
    activationSequenceTimer.invalidate();
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    activateMotorCount = 0;
    activateMotor = false;
}

bool AwakenSequence::transitionToIdle() {
    return activateMotorCount > AWAKEN_THRESHOLD;
    // return false;
}
