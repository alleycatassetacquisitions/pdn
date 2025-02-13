#include "game/handshake-machine.hpp"
#include "esp_log.h"

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
    ESP_LOGI("STARTING_LINE", "State mounted");
    if(player->isHunter()) {
        ESP_LOGI("STARTING_LINE", "Player is Hunter");
        handshakeSuccessfulFlag = true;
    } else {
        QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(std::bind(&StartingLineState::onQuickdrawCommandReceived, this, std::placeholders::_1));
    }
}

void StartingLineState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    ESP_LOGI("STARTING_LINE", "Command received: %d", command.command);
    if (command.command == STARTING_LINE) {
        ESP_LOGI("STARTING_LINE", "Received STARTING_LINE command");
        handshakeSuccessfulFlag = true;
    }
}

void StartingLineState::onStateLoop(Device *PDN) {
 
}

void StartingLineState::onStateDismounted(Device *PDN) {
    ESP_LOGI("STARTING_LINE", "State dismounted");
    handshakeSuccessfulFlag = false;
}

bool StartingLineState::handshakeSuccessful() {
    return handshakeSuccessfulFlag;
}
