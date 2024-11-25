#include "game/handshake-machine.hpp"
//
// Created by Elli Furedy on 10/1/2024.
//
/*
    if(!isHunter && peekGameComms() == HUNTER_HANDSHAKE_FINAL_ACK) {
      return true;
    }

    if(isHunter && peekGameComms() == BOUNTY_HANDSHAKE_FINAL_ACK) {
      return true;
    }
 */
HandshakeFinalAckState::HandshakeFinalAckState(Player *player) : State(HANDSHAKE_FINAL_ACK_STATE) {
    this->player = player;
}

HandshakeFinalAckState::~HandshakeFinalAckState() {
    player = nullptr;
}

void HandshakeFinalAckState::onStateMounted(Device *PDN) {
    std::vector<const string *> writing;
    std::vector<const string *> reading;
    if (player->isHunter()) {
        reading.push_back(&BOUNTY_HANDSHAKE_FINAL_ACK);

        writing.push_back(&HUNTER_HANDSHAKE_FINAL_ACK);
    } else {
        reading.push_back(&HUNTER_HANDSHAKE_FINAL_ACK);

        writing.push_back(&BOUNTY_HANDSHAKE_FINAL_ACK);
    }
    registerValidMessages(reading);
    registerResponseMessage(writing);

    PDN->writeString(writing[0]);
}


void HandshakeFinalAckState::onStateLoop(Device *PDN) {
    string *incomingMessage = waitForValidMessage(PDN);
    if (incomingMessage != nullptr) {
        handshakeSuccessfulFlag = true;
    }
}

void HandshakeFinalAckState::onStateDismounted(Device *PDN) {
    handshakeSuccessfulFlag = false;
}

bool HandshakeFinalAckState::handshakeSuccessful() {
    return handshakeSuccessfulFlag;
}
