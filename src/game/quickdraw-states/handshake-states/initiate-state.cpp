#include "game/quickdraw-states.hpp"
#include "esp_log.h"

HandshakeInitiateState::HandshakeInitiateState(Player *player) : BaseHandshakeState(HANDSHAKE_INITIATE_STATE) {
    this->player = player;
}

HandshakeInitiateState::~HandshakeInitiateState() {
    player = nullptr;
}

void HandshakeInitiateState::onStateMounted(Device *PDN) {
    ESP_LOGI("INITIATE_STATE", "State mounted");
}

void HandshakeInitiateState::onStateLoop(Device *PDN) {
    if(player->isHunter()) {
        ESP_LOGI("INITIATE_STATE", "Player is Hunter, transitioning to HunterSendIdState");
        transitionToHunterSendIdState = true;
    } else {
        ESP_LOGI("INITIATE_STATE", "Transitioning to BountySendCCState");
        transitionToBountySendCCState = true;
    }
}

void HandshakeInitiateState::onStateDismounted(Device *PDN) {
    ESP_LOGI("INITIATE_STATE", "State dismounted");
    transitionToBountySendCCState = false;
    transitionToHunterSendIdState = false;
}

bool HandshakeInitiateState::transitionToBountySendCC() {
    return transitionToBountySendCCState;
} 


bool HandshakeInitiateState::transitionToHunterSendId() {
    return transitionToHunterSendIdState;
}