#include "game/handshake-machine.hpp"
#include "id-generator.hpp"

//
// Created by Elli Furedy on 9/30/2024.
//

#include "wireless/quickdraw-wireless-manager.hpp"


BountySendConnectionConfirmedState::BountySendConnectionConfirmedState(Player *player) : State(BOUNTY_SEND_CC_STATE) {
    this->player = player;
}

BountySendConnectionConfirmedState::~BountySendConnectionConfirmedState() {
}



void BountySendConnectionConfirmedState::onStateMounted(Device *PDN) {
    string opponentId = player->getUserID(); // initially send our own id as opponent id
    string matchId = !player->isHunter() ? IdGenerator::GetInstance()->generateId() : ""; //only bounty generates match id

    QuickdrawWirelessManager::GetInstance()->broadcastPacket(*player->getOpponentMacAddress(), CONNECTION_CONFIRMED, 0 /*drawTimeMs*/, 0 /*ackCount*/, matchId, opponentId);

    transitionToBountySendAckState = true;
}

void BountySendConnectionConfirmedState::onStateDismounted(Device *PDN) {
    transitionToBountySendAckState = false;
}

bool BountySendConnectionConfirmedState::transitionToReceiveBattle() {
    return transitionToBountySendAckState;
}
