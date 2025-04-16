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
Win::Win(Player *player, WirelessManager* wirelessManager) : State(WIN) {
    this->player = player;
    this->wirelessManager = wirelessManager;
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

    winTimer.setTimer(8000);

    AnimationConfig config;
    config.type = player->isHunter() ? AnimationType::HUNTER_WIN : AnimationType::BOUNTY_WIN;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;

    PDN->startAnimation(config);
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

    PDN->setVibration(VIBRATION_OFF);
    
    // Switch to power-off WiFi mode at the end of game
    wirelessManager->powerOff();
}

bool Win::resetGame() {
    return reset;
}

bool Win::isTerminalState() {
    return true;
}
