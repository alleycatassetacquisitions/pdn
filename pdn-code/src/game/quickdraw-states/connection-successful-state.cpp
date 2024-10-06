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
ConnectionSuccessful::ConnectionSuccessful(Player *player) : State(CONNECTION_SUCCESSFUL) {
    this->player = player;
}

ConnectionSuccessful::~ConnectionSuccessful() {
    player = nullptr;
}


void ConnectionSuccessful::onStateMounted(Device *PDN) {
    //colors to indicate different powerups, types of players.
    if (player->isHunter()) {
        PDN->setGlobablLightColor(hunterColors[random8(16)]);
    } else {
        PDN->setGlobablLightColor(bountyColors[random8(16)]);
    }
}

void ConnectionSuccessful::onStateLoop(Device *PDN) {
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

void ConnectionSuccessful::onStateDismounted(Device *PDN) {
    PDN->setGlobalBrightness(255);
    lightsOn = false;
    alertCount = 0;
}

bool ConnectionSuccessful::transitionToCountdown() {
    return alertCount > transitionThreshold;
}
