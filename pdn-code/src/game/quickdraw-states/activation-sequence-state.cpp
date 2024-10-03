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

ActivationSequence::ActivationSequence() : State(ACTIVATION_SEQUENCE) {
}

void ActivationSequence::onStateMounted(Device *PDN) {
    activationSequenceTimer.setTimer(activationStepDuration);
    PDN->getVibrator().max();
}

void ActivationSequence::onStateLoop(Device *PDN) {
    activationSequenceTimer.updateTime();

    if(activationSequenceTimer.expired()) {
        if(activateMotorCount < 19) {
            if(activateMotor) {
                PDN->getVibrator().max();
            } else {
                PDN->getVibrator().off();
            }

            activationSequenceTimer.setTimer(activationStepDuration);
            activateMotorCount++;
            activateMotor = !activateMotor;
        }
    }
}

void ActivationSequence::onStateDismounted(Device *PDN) {
    activationSequenceTimer.invalidate();
    PDN->getVibrator().off();
    activateMotorCount = 0;
    activateMotor = true;
}

bool ActivationSequence::transitionToActivated() {
    return activateMotorCount > 19;
}