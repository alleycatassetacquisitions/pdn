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
Lose::Lose(Player *player) : State(LOSE) {
    this->player = player;
}

Lose::~Lose() {
    player = nullptr;
}


void Lose::onStateMounted(Device *PDN) {
    //Write match to eeprom.
    PDN->setVibration(VIBRATION_MAX);
}

void Lose::onStateLoop(Device *PDN) {
    EVERY_N_MILLIS(150) {
        PDN->setVibration(PDN->getCurrentVibrationIntensity() - 10);
    }

    if (PDN->getCurrentVibrationIntensity() <= 0) {
        reset = true;
    }
}

void Lose::onStateDismounted(Device *PDN) {
    reset = false;
}

bool Lose::resetGame() {
    return reset;
}

bool Lose::isTerminalState() {
    return true;
}
