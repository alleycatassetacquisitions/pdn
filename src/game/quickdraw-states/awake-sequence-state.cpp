#include "game/quickdraw-states.hpp"

/*
    bool activationSequence()
    {
      if (timerExpired())
      {
        if (activateMotorCount < 20)
        {
          if (activateMotor)
          {
            setMotorOutput(255);
          }
          else
          {
            setMotorOutput(0);
          }

          setTimer(100);
          activateMotorCount++;
          activateMotor = !activateMotor;
          return false;
        }
        else
        {
          activateMotorCount = 0;
          activateMotor = false;
          return true;
        }
      }
      return false;
    }
 */

AwakenSequence::AwakenSequence() : State(AWAKEN_SEQUENCE) {
}

void AwakenSequence::onStateMounted(Device *PDN) {
    activationSequenceTimer.setTimer(activationStepDuration);
    PDN->setVibration(VIBRATION_MAX);
}

void AwakenSequence::onStateLoop(Device *PDN) {
    activationSequenceTimer.updateTime();

    if (activationSequenceTimer.expired()) {
        if (activateMotorCount < 19) {
            if (activateMotor) {
                PDN->setVibration(VIBRATION_MAX);
            } else {
                PDN->setVibration(VIBRATION_OFF);
            }

            activationSequenceTimer.setTimer(activationStepDuration);
            activateMotorCount++;
            activateMotor = !activateMotor;
        }
    }
}

void AwakenSequence::onStateDismounted(Device *PDN) {
    activationSequenceTimer.invalidate();
    PDN->setVibration(VIBRATION_OFF);
    activateMotorCount = 0;
    activateMotor = true;
}

bool AwakenSequence::transitionToIdle() {
    return activateMotorCount > 19;
}
