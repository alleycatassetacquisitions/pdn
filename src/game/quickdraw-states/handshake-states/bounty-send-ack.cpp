#include "game/handshake-machine.hpp"
//
// Created by Elli Furedy on 10/1/2024.
//
/*
    if (isHunter)
    {
      if(gameCommsAvailable()) {
        current_opponent_id = readGameString('\r');
        writeGameComms(HUNTER_HANDSHAKE_FINAL_ACK);
        handshakeState = HANDSHAKE_STATE_FINAL_ACK;
        return false;
      }
    }
    else if (!isHunter)
    {
      if(gameCommsAvailable()) {
        current_match_id    = readGameString('\r');
        readGameString('\n');
        current_opponent_id = readGameString('\r');
        writeGameComms(BOUNTY_HANDSHAKE_FINAL_ACK);
        handshakeState = HANDSHAKE_STATE_FINAL_ACK;
        return false;
      }
    }
 */
BountySendFinalAckState::BountySendFinalAckState(Player *player) : State(BOUNTY_SEND_FINAL_ACK_STATE) {
    this->player = player;
}

BountySendFinalAckState::~BountySendFinalAckState() {
    player = nullptr;
    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}


void BountySendFinalAckState::onStateMounted(Device *PDN) {
    QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(std::bind(&BountySendFinalAckState::onQuickdrawCommandReceived, this, std::placeholders::_1));
}

void BountySendFinalAckState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    if (command.command == HUNTER_RECEIVE_MATCH) {
        player->setCurrentOpponentId(command.opponentId);
        player->setCurrentMatchId(command.matchId);
        QuickdrawWirelessManager::GetInstance()->broadcastPacket(*player->getOpponentMacAddress(), BOUNTY_RECEIVE_OPPONENT_ID, 0 /*drawTimeMs*/, 0 /*ackCount*/, *player->getCurrentMatchId(), *player->getCurrentOpponentId());
        transitionToStartingLineState = true;
    }
}





void BountySendFinalAckState::onStateLoop(Device *PDN) {
}

void BountySendFinalAckState::onStateDismounted(Device *PDN) {
    transitionToStartingLineState = false;
}

bool BountySendFinalAckState::transitionToStartingLine() {
    return transitionToStartingLineState;
}
