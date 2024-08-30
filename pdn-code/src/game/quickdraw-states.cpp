//
// Created by Elli Furedy on 8/30/2024.
//
#include "../../include/game/quickdraw-states.hpp"

#include "comms.hpp"
#include "game/quickdraw.hpp"

Dormant::Dormant() : QuickdrawState(DORMANT) {
}

void Dormant::onStateMounted(Device *PDN) {
    if(payload->isHunter) {
        dormantTimer.setTimer(5000);
    } else if(payload->debugDelay > 0) {
        dormantTimer.setTimer(payload->debugDelay);
    } else {
        unsigned long dormantTime = random(payload->bountyDelay[0], payload->bountyDelay[1]);
        dormantTimer.setTimer(dormantTime);
    }
}

void Dormant::onStateLoop(Device *PDN) {
    dormantTimer.updateTime();
}

void Dormant::onStateDismounted(Device *PDN) {
    dormantTimer.invalidate();
}

bool Dormant::transitionToActivationSequence() {
    return dormantTimer.expired();
}

ActivationSequence::ActivationSequence() : QuickdrawState(ACTIVATION_SEQUENCE) {
}

void ActivationSequence::onStateMounted(Device *PDN) {
    activationSequenceTimer.setTimer(payload->activationStepDuration);
}

void ActivationSequence::onStateLoop(Device *PDN) {
    activationSequenceTimer.updateTime();

    if(activationSequenceTimer.expired()) {
        if(payload->activateMotorCount < 19) {
            if(payload->activateMotor) {
                PDN->getVibrator().max();
            } else {
                PDN->getVibrator().off();
            }

            activationSequenceTimer.setTimer(payload->activationStepDuration);
            payload->activateMotorCount++;
            payload->activateMotor = !payload->activateMotor;
        }
    }
}

void ActivationSequence::onStateDismounted(Device *PDN) {
    activationSequenceTimer.invalidate();
    PDN->getVibrator().off();
}

bool ActivationSequence::transitionToActivated() {
    return payload->activateMotorCount > 19;
}

Activated::Activated() : QuickdrawState(ACTIVATED) {
}

void Activated::onStateMounted(Device *PDN) {
    if(payload->isHunter) {
        currentPalette = hunterColors;
    } else {
        currentPalette = bountyColors;
    }
}

void Activated::onStateLoop(Device *PDN) {

    EVERY_N_MILLIS(255) {
        broadcast();
    }

    EVERY_N_MILLIS(16) {
        ledAnimation(PDN);
    }
}

void Activated::onStateDismounted(Device *PDN) {
}

void Activated::broadcast() {
    if(payload->isHunter) {
        writeGameComms(HUNTER_BATTLE_MESSAGE);
    } else {
        writeGameComms(BOUNTY_BATTLE_MESSAGE);
    }
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
    if(gameCommsAvailable()) {
        if (gameCommsAvailable()) {
            if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && payload->isHunter) {
                readGameComms();
                writeGameComms(HUNTER_BATTLE_MESSAGE);
                return true;
            }
            if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !payload->isHunter) {
                readGameComms();
                writeGameComms(BOUNTY_BATTLE_MESSAGE);
                return true;
            }
        }
    }

    return false;
}

Handshake::Handshake() : QuickdrawState(HANDSHAKE) {
}

void Handshake::onStateMounted(Device *PDN) {
}

void Handshake::onStateLoop(Device *PDN) {
}

void Handshake::onStateDismounted(Device *PDN) {
}

DuelAlert::DuelAlert() : QuickdrawState(DUEL_ALERT) {
}

void DuelAlert::onStateMounted(Device *PDN) {
}

void DuelAlert::onStateLoop(Device *PDN) {
}

void DuelAlert::onStateDismounted(Device *PDN) {
}

DuelCountdown::DuelCountdown() : QuickdrawState(DUEL_COUNTDOWN) {
}

void DuelCountdown::onStateMounted(Device *PDN) {
}

void DuelCountdown::onStateLoop(Device *PDN) {
}

void DuelCountdown::onStateDismounted(Device *PDN) {
}

Duel::Duel() : QuickdrawState(DUEL) {

}

void Duel::onStateMounted(Device *PDN) {
}

void Duel::onStateLoop(Device *PDN) {
}

void Duel::onStateDismounted(Device *PDN) {
}

Win::Win() : QuickdrawState(WIN) {
}

void Win::onStateMounted(Device *PDN) {
}

void Win::onStateLoop(Device *PDN) {
}

void Win::onStateDismounted(Device *PDN) {
}

Lose::Lose() : QuickdrawState(LOSE) {
}

void Lose::onStateMounted(Device *PDN) {
}

void Lose::onStateLoop(Device *PDN) {
}

void Lose::onStateDismounted(Device *PDN) {
}
