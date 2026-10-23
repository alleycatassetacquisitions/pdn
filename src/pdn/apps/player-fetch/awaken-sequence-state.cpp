#include "apps/player-fetch/player-fetch-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"

PlayerFetchAwakenSequence::PlayerFetchAwakenSequence()
    : TypedState<PDN>(PlayerFetchStateId::AWAKEN) {}

PlayerFetchAwakenSequence::~PlayerFetchAwakenSequence() {}

void PlayerFetchAwakenSequence::onStateMounted(PDN* pdn) {
    activationSequenceTimer.setTimer(activationStepDuration);
    activateMotor = true;
    pdn->getHaptics()->setIntensity(125);

    pdn->getDisplay()
    ->invalidateScreen()
    ->drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))
    ->drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))
    ->render();
}

void PlayerFetchAwakenSequence::onStateLoop(PDN* pdn) {
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
        } else {
            pdn->getHaptics()->setIntensity(VIBRATION_OFF);
        }
    }
}

void PlayerFetchAwakenSequence::onStateDismounted(PDN* pdn) {
    activationSequenceTimer.invalidate();
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    activateMotorCount = 0;
    activateMotor = false;
}

bool PlayerFetchAwakenSequence::transitionToComplete() const {
    return activateMotorCount > AWAKEN_THRESHOLD;
}
