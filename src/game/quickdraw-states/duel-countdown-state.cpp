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
DuelCountdown::DuelCountdown(Player* player) : State(DUEL_COUNTDOWN) {
    this->player = player;
}

DuelCountdown::~DuelCountdown() {
    player = nullptr;
}


void DuelCountdown::onStateMounted(Device *PDN) {
    countdownQueue.push_back(THREE);
    countdownQueue.push_back(TWO);
    countdownQueue.push_back(ONE);

    const CountdownStage* current = countdownQueue.front();
    PDN->setGlobalBrightness(current->ledBrightness);

    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), getImageIdForStep(current->step)))->
    render();

    countdownTimer.setTimer(current->countdownTimer);
    countdownQueue.pop_front();
}


void DuelCountdown::onStateLoop(Device *PDN) {
    if (countdownTimer.expired()) {
        const CountdownStage* current = countdownQueue.front();
        if(current == nullptr) {
            doBattle = true;
        } else {
            PDN->setGlobalBrightness(current->ledBrightness);

            PDN->
            invalidateScreen()->
            drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), getImageIdForStep(current->step)))->
            render();

            countdownTimer.setTimer(current->countdownTimer);
            countdownQueue.pop_front();
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
    countdownTimer.invalidate();
}

bool DuelCountdown::shallWeBattle() {
    return doBattle;
}
