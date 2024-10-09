#include "game/quickdraw-states.hpp"
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
}

void Win::onStateLoop(Device *PDN) {
    EVERY_N_MILLIS(150) {
        PDN->setVibration(PDN->getCurrentVibrationIntensity() + 10);
    }
    if (PDN->getCurrentVibrationIntensity() > VIBRATION_MAX) {
        reset = true;
    }
}

void Win::onStateDismounted(Device *PDN) {
    reset = false;
}

bool Win::resetGame() {
    return reset;
}

bool Win::isTerminalState() {
    return true;
}
