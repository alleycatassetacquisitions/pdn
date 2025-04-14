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
Lose::Lose(Player *player, WirelessManager* wirelessManager) : State(LOSE) {
    this->player = player;
    this->wirelessManager = wirelessManager;
}

Lose::~Lose() {
    player = nullptr;
}


void Lose::onStateMounted(Device *PDN) {
    //Write match to eeprom.
    PDN->invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOSE))->
    render();

    loseTimer.setTimer(3000);

    AnimationConfig config;
    config.type = AnimationType::LOSE;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;

    PDN->startAnimation(config);
}

void Lose::onStateLoop(Device *PDN) {
    loseTimer.updateTime();
    if(loseTimer.expired()) {
        reset = true;
    }
}

void Lose::onStateDismounted(Device *PDN) {
    loseTimer.invalidate();
    reset = false;
    
    // Switch to power-off WiFi mode at the end of game
    wirelessManager->powerOff();
}

bool Lose::resetGame() {
    return reset;
}

bool Lose::isTerminalState() {
    return true;
}
