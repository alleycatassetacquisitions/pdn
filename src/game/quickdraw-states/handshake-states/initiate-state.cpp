#include "game/handshake-machine.hpp"

HandshakeInitiateState::HandshakeInitiateState(Player *player) : State(HANDSHAKE_INITIATE_STATE) {
    this->player = player;
}

HandshakeInitiateState::~HandshakeInitiateState() {
    player = nullptr;
}

void HandshakeInitiateState::onStateMounted(Device *PDN) {
    if(player->isHunter()) {
        transitionToHunterSendIdState = true;
    } else {
        transitionToBountySendCCState = true;
    }
}

void HandshakeInitiateState::onStateDismounted(Device *PDN) {
    transitionToBountySendCCState = false;
    transitionToHunterSendIdState = false;
}

bool HandshakeInitiateState::transitionToBountySendCC() {
    return transitionToBountySendCCState;
} 


bool HandshakeInitiateState::transitionToHunterSendId() {
    return transitionToHunterSendIdState;
}