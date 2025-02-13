#include "game/handshake-machine.hpp"
#include "esp_log.h"
//
// Created by Elli Furedy on 10/1/2024.
//

HunterSendFinalAckState::HunterSendFinalAckState(Player *player) : State(HUNTER_SEND_FINAL_ACK_STATE) {
    this->player = player;
}

HunterSendFinalAckState::~HunterSendFinalAckState() {
    player = nullptr;
}

void HunterSendFinalAckState::onStateMounted(Device *PDN) {
    ESP_LOGI("HUNTER_SEND_ACK", "State mounted");
    QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(std::bind(&HunterSendFinalAckState::onQuickdrawCommandReceived, this, std::placeholders::_1));
}

void HunterSendFinalAckState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    ESP_LOGI("HUNTER_SEND_ACK", "Command received: %d", command.command);
    if (command.command == BOUNTY_RECEIVE_OPPONENT_ID) {
        ESP_LOGI("HUNTER_SEND_ACK", "Received BOUNTY_RECEIVE_OPPONENT_ID command");
        transitionToStartingLineState = true;
    }
}

void HunterSendFinalAckState::onStateLoop(Device *PDN) {
}

void HunterSendFinalAckState::onStateDismounted(Device *PDN) {
    ESP_LOGI("HUNTER_SEND_ACK", "State dismounted");
    State::onStateDismounted(PDN);
    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

bool HunterSendFinalAckState::transitionToStartingLine() {
    return transitionToStartingLineState;
}
