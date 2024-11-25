#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
//
// Created by Elli Furedy on 9/30/2024.
//

/*
    void activationIdle()
    {
      // msgDelay was to prevent this from broadcasting every loop.
      if(msgDelay == 0) {
        comms().write(BATTLE_MESSAGE);
      }
      msgDelay = msgDelay + 1;
    }
 */
Idle::Idle(Player* player) : State(IDLE) {
    this->player = player;
    std::vector<const string *> writing;
    std::vector<const string *> reading;

    if (player->isHunter()) {
        reading.push_back(&BOUNTY_BATTLE_MESSAGE);
        writing.push_back(&HUNTER_BATTLE_MESSAGE);
    } else {
        reading.push_back(&HUNTER_BATTLE_MESSAGE);
        writing.push_back(&BOUNTY_BATTLE_MESSAGE);
    }

    registerValidMessages(reading);
    registerResponseMessage(writing);
}

Idle::~Idle() {
    player = nullptr;
}

void Idle::onStateMounted(Device *PDN) {

    if (player->isHunter()) {
        currentPalette = hunterColors;
    } else {
        currentPalette = bountyColors;
    }

    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
    render();
}

void Idle::onStateLoop(Device *PDN) {

    EVERY_N_MILLIS(250) {
        PDN->writeString(&responseStringMessages[0]);
    }

    EVERY_N_MILLIS(16) {
        ledAnimation(PDN);
    }

    string *validMessage = waitForValidMessage(PDN);
    if (validMessage != nullptr) {
        transitionToHandshakeState = true;
    }
}

void Idle::onStateDismounted(Device *PDN) {
    transitionToHandshakeState = false;
}

void Idle::ledAnimation(Device *PDN) {
    if (breatheUp) {
        ledBrightness++;
    } else {
        ledBrightness--;
    }
    pwm_val =
            255.0 * (1.0 - abs((2.0 * (ledBrightness / smoothingPoints)) - 1.0));

    if (ledBrightness == 255) {
        breatheUp = false;
    } else if (ledBrightness == 80) {
        breatheUp = true;
    }

    if (random8() % 8 == 0) {
        CRGB color = ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
        PDN->addToLight(LightIdentifier::DISPLAY_LIGHTS,
                        random8() % (numDisplayLights - 1),
                        LEDColor(color.r, color.g, color.b)
        );
    }


    for (int i = 0; i < numGripLights; i++) {
        if (random8() % 130 == 0) {
            CRGB color = ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
            PDN->addToLight(LightIdentifier::GRIP_LIGHTS,
                            i,
                            LEDColor(color.r, color.g, color.b)
            );
        }
    }

    if(random8() % 2) {
        PDN->fadeLightsBy(LightIdentifier::DISPLAY_LIGHTS, 1);
        PDN->fadeLightsBy(LightIdentifier::GRIP_LIGHTS, 1);
    }
}

bool Idle::transitionToHandshake() {
    return transitionToHandshakeState;
}
