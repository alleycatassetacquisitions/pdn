#include "game/quickdraw-states.hpp"
#include "id-generator.hpp"
#include "esp_log.h"

//
// Created by Elli Furedy on 9/30/2024.
//

#include "wireless/quickdraw-wireless-manager.hpp"


BountySendConnectionConfirmedState::BountySendConnectionConfirmedState(Player *player) : BaseHandshakeState(BOUNTY_SEND_CC_STATE) {
    this->player = player;
}

BountySendConnectionConfirmedState::~BountySendConnectionConfirmedState() {
    player = nullptr;
}



void BountySendConnectionConfirmedState::onStateMounted(Device *PDN) {
    
    ESP_LOGI("BOUNTY_SEND_CC", "State mounted");
    
    string opponentId = player->getUserID(); // initially send our own id as opponent id
    string matchId = !player->isHunter() ? IdGenerator::GetInstance()->generateId() : ""; //only bounty generates match id
    
    ESP_LOGI("BOUNTY_SEND_CC", "Broadcasting packet with matchId: %s, opponentId: %s", matchId.c_str(), opponentId.c_str());
    
    QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(std::bind(&BountySendConnectionConfirmedState::onQuickdrawCommandReceived, this, std::placeholders::_1));

    QuickdrawWirelessManager::GetInstance()->broadcastPacket(*player->getOpponentMacAddress(), CONNECTION_CONFIRMED, 0 /*drawTimeMs*/, 0 /*ackCount*/, matchId, opponentId);
}

void BountySendConnectionConfirmedState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    ESP_LOGI("BOUNTY_SEND_CC", "Received command: %d", command.command);
    if (command.command == HUNTER_RECEIVE_MATCH) {
        ESP_LOGI("BOUNTY_SEND_CC", "Received HUNTER_RECEIVE_MATCH command");
        player->setCurrentOpponentId(command.opponentId);
        player->setCurrentMatchId(command.matchId);
        QuickdrawWirelessManager::GetInstance()->broadcastPacket(*player->getOpponentMacAddress(), BOUNTY_FINAL_ACK, 0 /*drawTimeMs*/, 0 /*ackCount*/, *player->getCurrentMatchId(), *player->getCurrentOpponentId());
        transitionToConnectionSuccessfulState = true;
    }
}

void BountySendConnectionConfirmedState::onStateLoop(Device *PDN) {
    
}

void BountySendConnectionConfirmedState::onStateDismounted(Device *PDN) {
    transitionToConnectionSuccessfulState = false;
}

bool BountySendConnectionConfirmedState::transitionToConnectionSuccessful() {
    return transitionToConnectionSuccessfulState;
}
