//
// Created by Elli Furedy on 9/30/2024.
//
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"

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

Sleep::Sleep(Player* player, long debugDelay) : State(SLEEP) {
    this->player = player;
    this->debugDelay = debugDelay;
}

Sleep::~Sleep() {
    this->player = nullptr;
}

void Sleep::onStateMounted(Device *PDN) {
    if (player->isHunter()) {
        dormantTimer.setTimer(defaultDelay);
    } else if (debugDelay > 0) {
        dormantTimer.setTimer(debugDelay);
    } else {
        unsigned long dormantTime = random(bountyDelay[0], bountyDelay[1]);
        dormantTimer.setTimer(dormantTime);
    }

    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_LEFT))->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_RIGHT))->
    render();
}

void Sleep::onStateLoop(Device *PDN) {
    dormantTimer.updateTime();
}

void Sleep::onStateDismounted(Device *PDN) {
    dormantTimer.invalidate();
}

bool Sleep::transitionToAwakenSequence() {
    return dormantTimer.expired();
}
