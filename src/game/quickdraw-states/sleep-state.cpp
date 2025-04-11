//
// Created by Elli Furedy on 9/30/2024.
//
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include <string>
#include <clib/u8x8.h>
#include "game/quickdraw-requests.hpp"
#include <esp_log.h>

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
    PDN->
        invalidateScreen()->
        drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_LEFT))->
        drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_RIGHT))->
        render();
}

void Sleep::onStateLoop(Device *PDN) {

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

        CRGB color = ColorFromPalette(bountyColors, random8(), pwm_val, LINEARBLEND);
        PDN->setLight(LightIdentifier::TRANSMIT_LIGHT, 0, LEDColor(color.r, color.g, color.b));
    }
}

void Sleep::onStateDismounted(Device *PDN) {
    matchUploadRetryTimer.invalidate();
    dormantTimer.invalidate();
}

bool Sleep::transitionToAwakenSequence() {
    return dormantTimer.expired();
}