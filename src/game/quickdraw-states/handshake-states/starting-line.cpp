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
StartingLineState::StartingLineState(Player *player) : State(HANDSHAKE_STARTING_LINE_STATE) {
    this->player = player;
}

StartingLineState::~StartingLineState() {
    player = nullptr;
}

void StartingLineState::onStateMounted(Device *PDN) {
    if(player->isHunter()) {
        handshakeSuccessfulFlag = true;
    } else {
        QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(std::bind(&StartingLineState::onQuickdrawCommandReceived, this, std::placeholders::_1));
    }
}

void StartingLineState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    if (command.command == STARTING_LINE) {
        handshakeSuccessfulFlag = true;
    }
}

void StartingLineState::onStateLoop(Device *PDN) {
 
}

void StartingLineState::onStateDismounted(Device *PDN) {
    handshakeSuccessfulFlag = false;
}

bool StartingLineState::handshakeSuccessful() {
    return handshakeSuccessfulFlag;
}
