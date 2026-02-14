#include "apps/handshake/handshake-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"
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

    AnimationConfig config;
    config.type = AnimationType::TRANSMIT_BREATH;
    config.loop = true;
    config.speed = 5;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(bountyColors[0].red, bountyColors[0].green, bountyColors[0].blue), 255);
    PDN->getLightManager()->startAnimation(config);

    PDN->getDisplay()->
    invalidateScreen()->
    drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::CONNECT))->
    render();
    
    // Start the flash timer
    flashTimer.setTimer(flashDelay);
}

void ConnectionSuccessful::onStateLoop(Device *PDN) {
    if(flashTimer.expired()) {
        if (lightsOn) {
            PDN->getLightManager()->setGlobalBrightness(BRIGHTNESS_MAX);
        } else {
            PDN->getLightManager()->setGlobalBrightness(BRIGHTNESS_OFF);
        }

        lightsOn = !lightsOn;
        alertCount++;
        flashTimer.setTimer(flashDelay);
    }
}

void ConnectionSuccessful::onStateDismounted(Device *PDN) {
    PDN->getLightManager()->setGlobalBrightness(BRIGHTNESS_MAX);
    lightsOn = false;
    alertCount = 0;
}

bool ConnectionSuccessful::transitionToCountdown() {
    return alertCount > transitionThreshold;
}
