#include "game/handshake-machine.hpp"
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
    QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(std::bind(&HunterSendFinalAckState::onQuickdrawCommandReceived, this, std::placeholders::_1));
    
}

void HunterSendFinalAckState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    if (command.command == BOUNTY_RECEIVE_OPPONENT_ID) {
        transitionToStartingLineState = true;
    }
}

void HunterSendFinalAckState::onStateLoop(Device *PDN) {
}


void HunterSendFinalAckState::onStateDismounted(Device *PDN) {
    State::onStateDismounted(PDN);
    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

bool HunterSendFinalAckState::transitionToStartingLine() {
    return transitionToStartingLineState;
}
