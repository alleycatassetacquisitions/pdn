#include "game/quickdraw-states.hpp"
#include "id-generator.hpp"
#include "esp_log.h"

//
// Created by Elli Furedy on 9/30/2024.
//

#include "wireless/quickdraw-wireless-manager.hpp"


BountySendConnectionConfirmedState::BountySendConnectionConfirmedState(Player *player) : BaseHandshakeState(BOUNTY_SEND_CC_STATE, player) {
    // No need to set player here as it's already set in the BaseHandshakeState constructor
}

BountySendConnectionConfirmedState::~BountySendConnectionConfirmedState() {
}



void BountySendConnectionConfirmedState::onStateMounted(Device *PDN) {
    ESP_LOGI("BOUNTY_SEND_CC", "State mounted");
    string opponentId = player->getUserID(); // initially send our own id as opponent id
    string matchId = !player->isHunter() ? IdGenerator::GetInstance()->generateId() : ""; //only bounty generates match id
    ESP_LOGI("BOUNTY_SEND_CC", "Broadcasting packet with matchId: %s, opponentId: %s", matchId.c_str(), opponentId.c_str());
    QuickdrawWirelessManager::GetInstance()->broadcastPacket(*player->getOpponentMacAddress(), CONNECTION_CONFIRMED, 0 /*drawTimeMs*/, 0 /*ackCount*/, matchId, opponentId);
    delayTimer.setTimer(delay);
}

void BountySendConnectionConfirmedState::onStateLoop(Device *PDN) {
    transitionToBountySendAckState = delayTimer.expired();
}

void BountySendConnectionConfirmedState::onStateDismounted(Device *PDN) {
    ESP_LOGI("BOUNTY_SEND_CC", "State dismounted");
    transitionToBountySendAckState = false;
}

bool BountySendConnectionConfirmedState::transitionToBountySendAck() {
    return transitionToBountySendAckState;
}
