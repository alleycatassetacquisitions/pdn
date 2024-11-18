#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"

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
    activateMotor= true;
    PDN->setVibration(125);

    PDN->
    invalidateScreen()->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ENDLINE, ImageType::LOGO_LEFT))->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::HELIX, ImageType::LOGO_RIGHT))->
        render();
}

void AwakenSequence::onStateLoop(Device *PDN) {
    if (activationSequenceTimer.expired()) {
        if (activateMotorCount <= AWAKEN_THRESHOLD) {
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
    activateMotor = false;
}

bool AwakenSequence::transitionToIdle() {
    return activateMotorCount > AWAKEN_THRESHOLD;
    // return false;
}
