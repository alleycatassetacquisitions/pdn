#include "game/quickdraw-states.hpp"
//
// Created by Elli Furedy on 9/30/2024.
//
/*
    if (timerExpired()) {
    if (countdownStage == 4) {
      FastLED.showColor(currentPalette[0], 255);
      setTimer(FOUR);
      displayIsDirty = true;
      countdownStage = 3;
    } else if (countdownStage == 3) {
      FastLED.showColor(currentPalette[0], 150);
      setTimer(THREE);
      displayIsDirty = true;
      countdownStage = 2;
    } else if (countdownStage == 2) {
      FastLED.showColor(currentPalette[0], 75);
      setTimer(TWO);
      displayIsDirty = true;
      countdownStage = 1;
    } else if (countdownStage == 1) {
      FastLED.showColor(currentPalette[0], 0);
      setTimer(ONE);
      displayIsDirty = true;
      countdownStage = 0;
    } else if (countdownStage == 0) {
      doBattle = true;
    }
  }
 */
DuelCountdown::DuelCountdown() : State(DUEL_COUNTDOWN) {}

void DuelCountdown::onStateMounted(Device *PDN) {

}


void DuelCountdown::onStateLoop(Device *PDN) {
    countdownTimer.updateTime();
    if(countdownTimer.expired()) {
        switch(countdownStage) {
            case 4: {
                PDN->setGlobalBrightness(255);
                countdownTimer.setTimer(FOUR);
                countdownStage = 3;
                break;
            }
            case 3: {
                PDN->setGlobalBrightness(150);
                countdownTimer.setTimer(THREE);
                countdownStage = 2;
                break;
            }
            case 2: {
                PDN->setGlobalBrightness(75);
                countdownTimer.setTimer(TWO);
                countdownStage = 1;
                break;
            }
            case 1: {
                PDN->setGlobalBrightness(0);
                countdownTimer.setTimer(ONE);
                countdownStage = 0;
                break;
            }
            case 0: {
                doBattle = true;
            }
        }
    }
}

void DuelCountdown::onStateDismounted(Device *PDN) {
    countdownStage = COUNTDOWN_STAGES;
    doBattle = false;
    countdownTimer.invalidate();
}

bool DuelCountdown::shallWeBattle() {
    return doBattle;
}