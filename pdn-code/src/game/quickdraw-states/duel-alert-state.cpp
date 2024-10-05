#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
//
// Created by Elli Furedy on 9/30/2024.
//
/*
    if (alertCount == 0) {
    buttonBrightness = 255;
  }

  if (timerExpired()) {
    if (buttonBrightness == 255) {
      buttonBrightness = 0;
    } else {
      buttonBrightness = 255;
    }

    alertCount++;
    FastLED.setBrightness(buttonBrightness);
    setTimer(alertFlashTime);
  }
 */
DuelAlert::DuelAlert(Player *player) : State(DUEL_ALERT) {
    this->player = player;
}

DuelAlert::~DuelAlert() {
    player = nullptr;
}


void DuelAlert::onStateMounted(Device *PDN) {
    //colors to indicate different powerups, types of players.
    if (player->isHunter()) {
        PDN->setGlobablLightColor(hunterColors[random8(16)]);
    } else {
        PDN->setGlobablLightColor(bountyColors[random8(16)]);
    }
}

void DuelAlert::onStateLoop(Device *PDN) {
    EVERY_N_MILLIS(flashDelay) {
        if (lightsOn) {
            PDN->setGlobalBrightness(255);
        } else {
            PDN->setGlobalBrightness(0);
        }

        lightsOn = !lightsOn;
        alertCount++;
    }
}

void DuelAlert::onStateDismounted(Device *PDN) {
    PDN->setGlobalBrightness(255);
    lightsOn = false;
    alertCount = 0;
}

bool DuelAlert::transitionToCountdown() {
    return alertCount > transitionThreshold;
}
