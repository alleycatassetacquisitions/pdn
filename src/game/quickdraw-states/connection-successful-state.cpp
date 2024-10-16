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
    CRGB color;
    if (player->isHunter()) {
        color = hunterColors[random8(16)];
    } else {
        color = bountyColors[random8(16)];
    }
    PDN->setGlobablLightColor(LEDColor(color.r, color.g, color.b));

    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::CONNECT))->
    render();
}

void ConnectionSuccessful::onStateLoop(Device *PDN) {
    EVERY_N_MILLIS(flashDelay) {
        if (lightsOn) {
            PDN->setGlobalBrightness(BRIGHTNESS_MAX);
        } else {
            PDN->setGlobalBrightness(BRIGHTNESS_OFF);
        }

        lightsOn = !lightsOn;
        alertCount++;
    }
}

void ConnectionSuccessful::onStateDismounted(Device *PDN) {
    PDN->setGlobalBrightness(BRIGHTNESS_MAX);
    lightsOn = false;
    alertCount = 0;
}

bool ConnectionSuccessful::transitionToCountdown() {
    return alertCount > transitionThreshold;
}
