//
// Created by Elli Furedy on 9/30/2024.
//
#include "game/quickdraw-states.hpp"

/*
    void setupActivation()
    {
      if (isHunter)
      {
        // hunters have minimal activation delay
        setTimer(5000);
      }
      else
      {
        if (debugDelay > 0)
        {
          setTimer(debugDelay);
        }
        else if (bvbDuel)
        {
          randomSeed(analogRead(A0));
          setTimer(random(overchargeDelay[0], overchargeDelay[1]));
        }
        else
        {
          randomSeed(analogRead(A0));
          long timer = random(bountyDelay[0], bountyDelay[1]);
          Serial.print("timer: ");
          Serial.println(timer);
          setTimer(timer);
        }
      }

      activationInitiated = true;
    }
 */

Dormant::Dormant(bool isHunter, long debugDelay) : State(DORMANT) {
    this->isHunter = isHunter;
    this->debugDelay = debugDelay;
}

void Dormant::onStateMounted(Device *PDN) {
    if(isHunter) {
        dormantTimer.setTimer(defaultDelay);
    } else if(debugDelay > 0) {
        dormantTimer.setTimer(debugDelay);
    } else {
        unsigned long dormantTime = random(bountyDelay[0], bountyDelay[1]);
        dormantTimer.setTimer(dormantTime);
    }
}

void Dormant::onStateLoop(Device *PDN) {
    dormantTimer.updateTime();
}

void Dormant::onStateDismounted(Device *PDN) {
    dormantTimer.invalidate();
}

bool Dormant::transitionToActivationSequence() {
    return dormantTimer.expired();
}