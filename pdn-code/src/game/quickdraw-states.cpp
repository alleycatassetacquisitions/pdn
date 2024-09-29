//
// Created by Elli Furedy on 8/30/2024.
//
#include "../../include/game/quickdraw-states.hpp"

#include "comms.hpp"
#include "game/quickdraw.hpp"

Dormant::Dormant(bool isHunter, long debugDelay) : State(DORMANT) {
    this->isHunter = isHunter;
    this->debugDelay = debugDelay;
}

void Dormant::onStateMounted(Device *PDN) {
    if(isHunter) {
        dormantTimer.setTimer(5000);
    } else if(debugDelay > 0) {
        dormantTimer.setTimer(debugDelay);
    } else {
        unsigned long dormantTime = random(bountyDelay[0], bountyDelay[1]);
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

ActivationSequence::ActivationSequence() : State(ACTIVATION_SEQUENCE) {
}

void ActivationSequence::onStateMounted(Device *PDN) {
    activationSequenceTimer.setTimer(activationStepDuration);
    PDN->getVibrator().max();
}

void ActivationSequence::onStateLoop(Device *PDN) {
    activationSequenceTimer.updateTime();

    if(activationSequenceTimer.expired()) {
        if(activateMotorCount < 19) {
            if(activateMotor) {
                PDN->getVibrator().max();
            } else {
                PDN->getVibrator().off();
            }

            activationSequenceTimer.setTimer(activationStepDuration);
            activateMotorCount++;
            activateMotor = !activateMotor;
        }
    }
}

void ActivationSequence::onStateDismounted(Device *PDN) {
    activationSequenceTimer.invalidate();
    PDN->getVibrator().off();
    activateMotorCount = 0;
    activateMotor = true;
}

bool ActivationSequence::transitionToActivated() {
    return activateMotorCount > 19;
}

Activated::Activated(bool isHunter) : State(ACTIVATED) {
    this->isHunter = isHunter;
}

void Activated::onStateMounted(Device *PDN) {
    if(isHunter) {
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
    if(isHunter) {
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
    if (gameCommsAvailable()) {
        if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && isHunter) {
            readGameComms();
            writeGameComms(HUNTER_BATTLE_MESSAGE);
            return true;
        }
        if(peekGameComms() == HUNTER_BATTLE_MESSAGE && isHunter) {
            readGameComms();
            writeGameComms(BOUNTY_BATTLE_MESSAGE);
            return true;
        }
    }

    return false;
}

Handshake::Handshake(Player* player) : State(HANDSHAKE), stateMachine(player) {
    this->player = player;
}

Handshake::~Handshake() {
    player = nullptr;
}


void Handshake::onStateMounted(Device *PDN) {
    stateMachine.initialize();
    handshakeTimeout.setTimer(timeout);
}

void Handshake::onStateLoop(Device *PDN) {
    if(handshakeTimeout.expired()) {
        resetToActivated = true;
    } else {
        stateMachine.loop();
    }
}

void Handshake::onStateDismounted(Device *PDN) {
    handshakeTimeout.invalidate();
    resetToActivated = false;
}

bool Handshake::transitionToActivated() {
    return resetToActivated;
}

bool Handshake::transitionToDuelAlert() {
    return stateMachine.handshakeSuccessful();
}

DuelAlert::DuelAlert(Player* player) : State(DUEL_ALERT) {
    this->player = player;
}

DuelAlert::~DuelAlert() {
    player = nullptr;
}


void DuelAlert::onStateMounted(Device *PDN) {
    if(player->isHunter()) {
        PDN->setGlobablLightColor(hunterColors[random8(16)]);
    } else {
        PDN->setGlobablLightColor(bountyColors[random8(16)]);
    }
}

void DuelAlert::onStateLoop(Device *PDN) {
    EVERY_N_MILLIS(flashDelay) {
        if(lightsOn) {
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

DuelCountdown::DuelCountdown() : State(DUEL_COUNTDOWN) {}

void DuelCountdown::onStateLoop(Device *PDN) {
    if(countdownTimer.expired()) {
        switch(countdownStage) {
            case 4: {
                PDN->setGlobalBrightness(255);
                countdownTimer.setTimer(FOUR);
                countdownStage = 3;
                break;
            }
            case 3: {
                PDN->setGlobalBrightness(150);
                countdownTimer.setTimer(THREE);
                countdownStage = 2;
                break;
            }
            case 2: {
                PDN->setGlobalBrightness(75);
                countdownTimer.setTimer(TWO);
                countdownStage = 1;
                break;
            }
            case 1: {
                PDN->setGlobalBrightness(0);
                countdownTimer.setTimer(ONE);
                countdownStage = 0;
                break;
            }
            case 0: {
                doBattle = true;
            }
        }
    }
}

void DuelCountdown::onStateDismounted(Device *PDN) {
    countdownStage = COUNTDOWN_STAGES;
    doBattle = false;
    countdownTimer.invalidate();
}

bool DuelCountdown::shallWeBattle() {
    return doBattle;
}


Duel::Duel() : State(DUEL) {
}

void Duel::onStateMounted(Device *PDN) {
    PDN->attachPrimaryButtonClick([]() {
        writeGameComms(ZAP);
    });
    PDN->attachSecondaryButtonClick([]() {
        writeGameComms(ZAP);
    });

    duelTimer.setTimer(DUEL_TIMEOUT);
}

void Duel::onStateLoop(Device *PDN) {
    if(peekGameComms() == ZAP) {
        readGameComms();
        writeGameComms(YOU_DEFEATED_ME);
        captured = true;
        return;
    }
    if(peekGameComms() == YOU_DEFEATED_ME) {
        readGameComms();
        wonBattle = true;
        return;
    }

    //only enable the button if they haven't been defeated.
    if(!captured) {
        PDN->getPrimaryButton().tick();
    }

}

bool Duel::transitionToActivated() {
    return duelTimer.expired();
}

bool Duel::transitionToWin() {
    return wonBattle;
}

bool Duel::transitionToLose() {
    return captured;
}

void Duel::onStateDismounted(Device *PDN) {
    duelTimer.invalidate();
    wonBattle = false;
    captured = false;
}

Win::Win(Player* player) : State(WIN) {
    this->player = player;
}

Win::~Win() {
    player = nullptr;
}


void Win::onStateMounted(Device *PDN) {
    //Write to EEPROM. Don't have that figured out yet.
    PDN->getVibrator().off();
}

void Win::onStateLoop(Device *PDN) {
    EVERY_N_MILLIS(150) {
        PDN->getVibrator().setIntensity(PDN->getVibrator().getIntensity()+10);
    }
    if(PDN->getVibrator().getIntensity() > 255) {
        reset = true;
    }
}

void Win::onStateDismounted(Device *PDN) {
    reset = false;
}

bool Win::resetGame() {
    return reset;
}

Lose::Lose(Player* player) : State(LOSE) {
    this->player = player;
}

Lose::~Lose() {
    player = nullptr;
}


void Lose::onStateMounted(Device *PDN) {
    //Write match to eeprom.
    PDN->getVibrator().max();
}

void Lose::onStateLoop(Device *PDN) {
    EVERY_N_MILLIS(150) {
        PDN->getVibrator().setIntensity(PDN->getVibrator().getIntensity()-10);
    }

    if(PDN->getVibrator().getIntensity() <= 0) {
        reset = true;
    }
}

void Lose::onStateDismounted(Device *PDN) {
    reset = false;
}

bool Lose::resetGame() {
    return reset;
}