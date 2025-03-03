#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
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

    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = player->isHunter() ? HUNTER_IDLE_STATE_ALTERNATE : BOUNTY_IDLE_STATE_ALTERNATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->startAnimation(config);
}

void Idle::onStateLoop(Device *PDN) {

    EVERY_N_MILLIS(250) {
        PDN->writeString(&responseStringMessages[0]);
    }



    string *validMessage = waitForValidMessage(PDN);
    if (validMessage != nullptr) {
        transitionToHandshakeState = true;
    }
}

void Idle::onStateDismounted(Device *PDN) {
    transitionToHandshakeState = false;
    cycleDelayTimer.invalidate();
    isWaitingBetweenCycles = false;
}

bool Idle::transitionToHandshake() {
    return transitionToHandshakeState;
}
