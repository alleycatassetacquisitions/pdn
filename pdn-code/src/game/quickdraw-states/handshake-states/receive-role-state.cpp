#include "game/handshake-machine.hpp"
#include "comms_constants.hpp"
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
HandshakeReceiveRoleState::HandshakeReceiveRoleState(Player *player) : State(HANDSHAKE_RECEIVED_ROLE_STATE) {
    this->player = player;
    std::vector<const String *> writing;
    std::vector<const String *> reading;
    if (player->isHunter()) {
        reading.push_back(&SEND_USER_ID);

        writing.push_back(&HUNTER_HANDSHAKE_FINAL_ACK);
    } else {
        reading.push_back(&SEND_USER_ID);
        reading.push_back(&SEND_MATCH_ID);

        writing.push_back(&BOUNTY_HANDSHAKE_FINAL_ACK);
    }
    registerValidMessages(reading);
    registerResponseMessage(writing);
}

HandshakeReceiveRoleState::~HandshakeReceiveRoleState() {
    player = nullptr;
}

void HandshakeReceiveRoleState::onStateMounted(Device *PDN) {
}

void HandshakeReceiveRoleState::onStateLoop(Device *PDN) {
    String *incomingMessage = waitForValidMessage(PDN);
    if (incomingMessage != nullptr) {
        if (*incomingMessage == SEND_USER_ID) {
            String userId = PDN->readString();
            player->setCurrentOpponentId(userId);
        }
        if (*incomingMessage == SEND_MATCH_ID) {
            String matchId = PDN->readString();
            player->setCurrentMatchId(matchId);
        }
    }

    if (player->getCurrentMatchId() != nullptr && player->getCurrentOpponentId() != nullptr) {
        for (int i = 0; i < responseStringMessages.size(); i++) {
            PDN->writeString(&responseStringMessages[i]);
        }
        transitionToFinalAckState = true;
    }

    // if(player->isHunter()) {
    //     if(gameCommsAvailable()) {
    //         String opponentId = readGameString('\r');
    //         player->setCurrentOpponentId(&opponentId);
    //         transitionToFinalAckState = true;
    //     }
    // } else if(!player->isHunter()) {
    //     if(gameCommsAvailable()) {
    //         String currentMatchId = readGameString('\r');
    //         player->setCurrentMatchId(&currentMatchId);
    //         String opponentId = readGameString('\r');
    //         player->setCurrentOpponentId(&opponentId);
    //         transitionToFinalAckState = true;
    //     }
    // }
}

void HandshakeReceiveRoleState::onStateDismounted(Device *PDN) {
    transitionToFinalAckState = false;
}

bool HandshakeReceiveRoleState::transitionToFinalAck() {
    return transitionToFinalAckState;
}
