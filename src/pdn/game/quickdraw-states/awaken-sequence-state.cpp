#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"

AwakenSequence::AwakenSequence(const GameContext& ctx)
    : TypedState<PDN>(AWAKEN_SEQUENCE)
    , player(ctx.player) {}

void AwakenSequence::onStateMounted(PDN* pdn) {
    activationSequenceTimer.setTimer(activationStepDuration);
    pdn->getHaptics()->setIntensity(125);

    pdn->getDisplay()
    ->invalidateScreen()
    ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_LEFT))
    ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::STAMP))
    ->render();
}

void AwakenSequence::onStateLoop(PDN* pdn) {
    // No step past the last: transitionToIdle holds as soon as the count
    // reaches the threshold, and leaving turns the motor off.
    if (activationSequenceTimer.expired()) {
        pdn->getHaptics()->setIntensity(activateMotorCount % 2 == 0 ? VIBRATION_MAX
                                                                    : VIBRATION_OFF);
        activationSequenceTimer.setTimer(activationStepDuration);
        activateMotorCount++;
    }
}

void AwakenSequence::onStateDismounted(PDN* pdn) {
    activationSequenceTimer.invalidate();
    pdn->getHaptics()->setIntensity(VIBRATION_OFF);
    activateMotorCount = 0;
}

bool AwakenSequence::transitionToIdle() const { return activateMotorCount >= AWAKEN_THRESHOLD; }
