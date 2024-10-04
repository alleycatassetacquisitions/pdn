#include "game/quickdraw-states.hpp"
//
// Created by Elli Furedy on 9/30/2024.
//
Handshake::Handshake(Player* player) : State(HANDSHAKE) {
    this->player = player;
}

Handshake::~Handshake() {
    player = nullptr;
    stateMachine = nullptr;
}


void Handshake::onStateMounted(Device *PDN) {
    stateMachine = new HandshakeStateMachine(player, PDN);
    stateMachine->initialize();
    handshakeTimeout.setTimer(timeout);
}

void Handshake::onStateLoop(Device *PDN) {
    handshakeTimeout.updateTime();

    if(handshakeTimeout.expired()) {
        resetToActivated = true;
    } else {
        stateMachine->loop();
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
    return stateMachine->handshakeSuccessful();
}