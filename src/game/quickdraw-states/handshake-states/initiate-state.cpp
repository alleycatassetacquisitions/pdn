#include "game/quickdraw-states.hpp"
#include "device/wireless-manager.hpp"

HandshakeInitiateState::HandshakeInitiateState(Player *player) : BaseHandshakeState(HANDSHAKE_INITIATE_STATE) {
    this->player = player;
}

HandshakeInitiateState::~HandshakeInitiateState() {
    player = nullptr;
}

void HandshakeInitiateState::onStateMounted(Device *PDN) {
    LOG_I("INITIATE_STATE", "State mounted");
}

void HandshakeInitiateState::onStateLoop(Device *PDN) {
    BaseHandshakeState::initTimeout();
    if(player->isHunter()) {
        LOG_I("INITIATE_STATE", "Player is Hunter, transitioning to HunterSendIdState");
        transitionToHunterSendIdState = true;
    } else {
        LOG_I("INITIATE_STATE", "Transitioning to BountySendCCState");
        transitionToBountySendCCState = true;
    }
}

void HandshakeInitiateState::onStateDismounted(Device *PDN) {
    LOG_I("INITIATE_STATE", "State dismounted");
    transitionToBountySendCCState = false;
    transitionToHunterSendIdState = false;
}

bool HandshakeInitiateState::transitionToBountySendCC() {
    return transitionToBountySendCCState;
} 


bool HandshakeInitiateState::transitionToHunterSendId() {
    return transitionToHunterSendIdState;
}