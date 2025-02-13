#include "game/handshake-machine.hpp"
#include "esp_log.h"

HandshakeInitiateState::HandshakeInitiateState(Player *player) : State(HANDSHAKE_INITIATE_STATE) {
    this->player = player;
}

HandshakeInitiateState::~HandshakeInitiateState() {
    player = nullptr;
}

void HandshakeInitiateState::onStateMounted(Device *PDN) {
    ESP_LOGI("INITIATE_STATE", "State mounted");
    handshakeSettlingTimer.setTimer(HANDSHAKE_SETTLE_TIME);
}

void HandshakeInitiateState::onStateLoop(Device *PDN) {
    ESP_LOGI("INITIATE_STATE", "State loop running");
    handshakeSettlingTimer.updateTime();
    ESP_LOGI("INITIATE_STATE", "Timer expired: %d", handshakeSettlingTimer.expired());
    if(player->isHunter()) {
        ESP_LOGI("INITIATE_STATE", "Player is Hunter");
        transitionToHunterSendIdState = true;
    } else {
        if(handshakeSettlingTimer.expired()) {
            ESP_LOGI("INITIATE_STATE", "Transitioning to BountySendCCState");
            transitionToBountySendCCState = true;
        }
    }
}

void HandshakeInitiateState::onStateDismounted(Device *PDN) {
    ESP_LOGI("INITIATE_STATE", "State dismounted");
    transitionToBountySendCCState = false;
    transitionToHunterSendIdState = false;
    handshakeSettlingTimer.invalidate();
}

bool HandshakeInitiateState::transitionToBountySendCC() {
    return transitionToBountySendCCState;
} 


bool HandshakeInitiateState::transitionToHunterSendId() {
    return transitionToHunterSendIdState;
}