#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
//
// Created by Elli Furedy on 9/30/2024.
//
/*
if (startBattleFinish) {
    startBattleFinish = false;
    setMotorOutput(255);
  }

  if (timerExpired()) {
    setTimer(500);
    if (finishBattleBlinkCount < FINISH_BLINKS) {
      if (finishBattleBlinkCount % 2 == 0) {
        setMotorOutput(0);
      } else {
        setMotorOutput(255);
      }
      finishBattleBlinkCount = finishBattleBlinkCount + 1;
    } else {
      setMotorOutput(0);
      reset = true;
    }
  }
 */
Win::Win(Player *player) : State(WIN) {
    this->player = player;
}

Win::~Win() {
    player = nullptr;
}


void Win::onStateMounted(Device *PDN) {
    //Write to EEPROM. Don't have that figured out yet.
    PDN->setVibration(VIBRATION_OFF);

    PDN->invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::WIN))->
    render();

    winTimer.setTimer(3000);
}

void Win::onStateLoop(Device *PDN) {
    winTimer.updateTime();
    if(winTimer.expired()) {
        reset = true;
    }
}

void Win::onStateDismounted(Device *PDN) {
    winTimer.invalidate();
    reset = false;
}

bool Win::resetGame() {
    return reset;
}

bool Win::isTerminalState() {
    return true;
}
