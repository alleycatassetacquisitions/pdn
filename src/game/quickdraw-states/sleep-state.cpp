//
// Created by Elli Furedy on 9/30/2024.
//
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include <string>
#include "game/quickdraw-requests.hpp"

#define TAG "SleepState"

using namespace std;
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

Sleep::Sleep(Player* player) : State(SLEEP) {
    this->player = player;
}

Sleep::~Sleep() {
    this->player = nullptr;
}

void Sleep::onStateMounted(Device *PDN) {
    PDN->getDisplay()->
        invalidateScreen()->
        drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_LEFT))->
        drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_RIGHT))->
        render();

    dormantTimer.setTimer(SLEEP_DURATION);
}

void Sleep::onStateLoop(Device *PDN) {

    dormantTimer.updateTime();

    if(dormantTimer.expired()) {
        transitionToAwakenSequenceState = true;
    }

    // TODO: Convert this breathing effect to use the new animation system
    // The old direct LED control API (setLight) has been removed in favor of animations
    // This breathing effect should be implemented as a proper Animation class
    /*
    EVERY_N_MILLIS(16) {
        if (breatheUp) {
            ledBrightness++;
        } else {
            ledBrightness--;
        }
        pwm_val =
            255.0 * (1.0 - abs((2.0 * (ledBrightness / smoothingPoints)) - 1.0));

        if (ledBrightness == 255) {
            breatheUp = false;
        } else if (ledBrightness == 0) {
            breatheUp = true;
        }

        LEDColor color = bountyColors[random8()];
        PDN->getLightManager()->setLight(LightIdentifier::TRANSMIT_LIGHT, 0, color);
    }
    */
}

void Sleep::onStateDismounted(Device *PDN) {
    dormantTimer.invalidate();
}

bool Sleep::transitionToAwakenSequence() {
    return transitionToAwakenSequenceState;
}