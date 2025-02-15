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

    EVERY_N_MILLIS(500) {
        ledAnimation(PDN);
    }

    EVERY_N_MILLIS(50) {
        PDN->fadeLightsBy(LightIdentifier::GLOBAL, 25);
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
    CRGB color;
    if(ledChaseIndex == 2) {
        color = CRGB::Red;
    } else if(ledChaseIndex == 1) {
        color = CRGB::OrangeRed;
    } else {
        color = CRGB::Orange;
    }

    pwm_val = 255.0 * (1.0 - abs((2.0 * (ledBrightness[ledChaseIndex] / smoothingPoints)) - 1.0));

    color.nscale8_video(ledBrightness);
    
    Serial.printf("Current chase index: %d\n", ledChaseIndex);
    PDN->setLight(LightIdentifier::LEFT_LIGHTS, ledChaseIndex, LEDColor(color.r, color.g, color.b));
    PDN->setLight(LightIdentifier::RIGHT_LIGHTS, ledChaseIndex, LEDColor(color.r, color.g, color.b));


    ledChaseIndex--;
    if(ledChaseIndex < 0) {
        ledChaseIndex = 2;
    }
    // if (breatheUp) {
    //     ledBrightness++;
    // } else {
    //     ledBrightness--;
    // }
    // pwm_val =
    //         255.0 * (1.0 - abs((2.0 * (ledBrightness / smoothingPoints)) - 1.0));

    // if (ledBrightness == 255) {
    //     breatheUp = false;
    // } else if (ledBrightness == 80) {
    //     breatheUp = true;
    // }

    // if (random8() % 8 == 0) {
    //     CRGB color = ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
    //     PDN->addToLight(LightIdentifier::DISPLAY_LIGHTS,
    //                     random8() % (numDisplayLights - 1),
    //                     LEDColor(color.r, color.g, color.b)
    //     );
    // }


    // for (int i = 0; i < numGripLights; i++) {
    //     if (random8() % 130 == 0) {
    //         CRGB color = ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
    //         PDN->addToLight(LightIdentifier::GRIP_LIGHTS,
    //                         i,
    //                         LEDColor(color.r, color.g, color.b)
    //         );
    //     }
    // }

    // if(random8() % 2) {
    //     PDN->fadeLightsBy(LightIdentifier::DISPLAY_LIGHTS, 1);
    //     PDN->fadeLightsBy(LightIdentifier::GRIP_LIGHTS, 1);
    // }
}

bool Idle::transitionToHandshake() {
    return transitionToHandshakeState;
}
