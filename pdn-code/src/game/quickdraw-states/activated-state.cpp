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
Activated::Activated(bool isHunter) : State(ACTIVATED) {
    this->isHunter = isHunter;
    std::vector<const String *> writing;
    std::vector<const String *> reading;

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

void Activated::onStateMounted(Device *PDN) {
    if (isHunter) {
        currentPalette = hunterColors;
    } else {
        currentPalette = bountyColors;
    }
}

void Activated::onStateLoop(Device *PDN) {
    //This may be totally bad, but trying to figure out how to not spam Serial.
    if (PDN->getTrxBufferedMessagesSize() == 0) {
        PDN->writeString(&responseStringMessages[0]);
    }

    EVERY_N_MILLIS(16) {
        ledAnimation(PDN);
    }

    String *validMessage = waitForValidMessage(PDN);
    if (validMessage != nullptr) {
        transitionToHandshakeState = true;
    }
}

void Activated::onStateDismounted(Device *PDN) {
    transitionToHandshakeState = false;
}

void Activated::ledAnimation(Device *PDN) {
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
        PDN->getDisplayLights().addToLight(
            random8() % (numDisplayLights - 1),
            ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND)
        );
    }
    PDN->getDisplayLights().fade(2);

    for (int i = 0; i < numGripLights; i++) {
        if (random8() % 65 == 0) {
            PDN->getGripLights().addToLight(
                i,
                ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND)
            );
        }
    }
    PDN->getGripLights().fade(2);
}

bool Activated::transitionToHandshake() {
    return transitionToHandshakeState;
}
