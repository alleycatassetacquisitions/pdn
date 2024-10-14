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
Idle::Idle(bool isHunter) : State(IDLE) {
    this->isHunter = isHunter;
    std::vector<const string *> writing;
    std::vector<const string *> reading;

    if (isHunter) {
        reading.push_back(&BOUNTY_BATTLE_MESSAGE);
        writing.push_back(&HUNTER_BATTLE_MESSAGE);
    } else {
        reading.push_back(&HUNTER_BATTLE_MESSAGE);
        writing.push_back(&BOUNTY_BATTLE_MESSAGE);
    }

    registerValidMessages(reading);
    registerResponseMessage(writing);
}

void Idle::onStateMounted(Device *PDN) {
    if (isHunter) {
        currentPalette = hunterColors;
    } else {
        currentPalette = bountyColors;
    }
}

void Idle::onStateLoop(Device *PDN) {
    //This may be totally bad, but trying to figure out how to not spam Serial.
    if (PDN->getSerialWriteQueueSize() == 0) {
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
    } else if (ledBrightness == 0) {
        breatheUp = true;
    }

    if (random8() % 7 == 0) {
        CRGB color = ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
        PDN->addToLight(DISPLAY_LIGHTS,
            random8() % (numDisplayLights - 1),
            LEDColor(color.r, color.g, color.b)
        );
    }
    PDN->fadeLightsBy(DISPLAY_LIGHTS, 2);

    for (int i = 0; i < numGripLights; i++) {
        if (random8() % 65 == 0) {
            CRGB color = ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
            PDN->addToLight(GRIP_LIGHTS,
                i,
                LEDColor(color.r, color.g, color.b)
            );
        }
    }
    PDN->fadeLightsBy(GRIP_LIGHTS, 2);
}

bool Idle::transitionToHandshake() {
    return transitionToHandshakeState;
}
