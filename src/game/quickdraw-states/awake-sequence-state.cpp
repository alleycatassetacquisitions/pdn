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

AwakenSequence::AwakenSequence(Player* player) : State(AWAKEN_SEQUENCE) {
    this->player = player;
}

AwakenSequence::~AwakenSequence() {
    player = nullptr;
}

void AwakenSequence::onStateMounted(Device *PDN) {
    activationSequenceTimer.setTimer(activationStepDuration);
    activateMotor= true;
    PDN->getHaptics()->setIntensity(125);

    PDN->getDisplay()->
    invalidateScreen()->
        drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_LEFT))->
        drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::STAMP))->
        render();
}

void AwakenSequence::onStateLoop(Device *PDN) {
    if (activationSequenceTimer.expired()) {
        if (activateMotorCount <= AWAKEN_THRESHOLD) {
            if (activateMotor) {
                PDN->getHaptics()->setIntensity(VIBRATION_MAX);
            } else {
                PDN->getHaptics()->setIntensity(VIBRATION_OFF);
            }

            activationSequenceTimer.setTimer(activationStepDuration);
            activateMotorCount++;
            activateMotor = !activateMotor;
        }
    }
}

void AwakenSequence::onStateDismounted(Device *PDN) {
    activationSequenceTimer.invalidate();
    PDN->getHaptics()->setIntensity(VIBRATION_OFF);
    activateMotorCount = 0;
    activateMotor = false;
}

bool AwakenSequence::transitionToIdle() {
    return activateMotorCount > AWAKEN_THRESHOLD;
    // return false;
}
