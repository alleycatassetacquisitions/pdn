#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
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
DuelCountdown::DuelCountdown(Player* player, MatchManager* matchManager) : State(DUEL_COUNTDOWN) {
    this->player = player;
    this->matchManager = matchManager;
}

DuelCountdown::~DuelCountdown() {
    player = nullptr;
    matchManager = nullptr;
}


void DuelCountdown::onStateMounted(Device *PDN) {
    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), getImageIdForStep(countdownQueue[currentStepIndex].step)))->
    render();

    countdownTimer.setTimer(countdownQueue[currentStepIndex].countdownTimer);
    currentStepIndex++;

    AnimationConfig config;
    config.type = AnimationType::COUNTDOWN;
    config.speed = 18;
    config.loopDelayMs = 0;
    config.loop = false;
    PDN->startAnimation(config);
}


void DuelCountdown::onStateLoop(Device *PDN) {
    countdownTimer.updateTime();
    if (countdownTimer.expired()) {
        if(countdownQueue[currentStepIndex].step == CountdownStep::BATTLE) {
            doBattle = true;
        } else {
            PDN->
            invalidateScreen()->
            drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), getImageIdForStep(countdownQueue[currentStepIndex].step)))->
            render();

            countdownTimer.setTimer(countdownQueue[currentStepIndex].countdownTimer);
            currentStepIndex++;
        }
    }
}

ImageType DuelCountdown::getImageIdForStep(CountdownStep step) {
    switch(step) {
        case CountdownStep::THREE:
            return ImageType::COUNTDOWN_THREE;
        case CountdownStep::TWO:
            return ImageType::COUNTDOWN_TWO;
        default:
            return ImageType::COUNTDOWN_ONE;
    }
}


void DuelCountdown::onStateDismounted(Device *PDN) {
    doBattle = false;
    currentStepIndex = 0;
    countdownTimer.invalidate();
}

bool DuelCountdown::shallWeBattle() {
    return doBattle;
}
