#include "game/handshake-machine.hpp"
#include "comms_constants.hpp"
//
// Created by Elli Furedy on 10/1/2024.
//
/*
if(peekGameComms() == BOUNTY_SHAKE && isHunter)
    {
      writeGameString(current_match_id);         // Sending match_id
      writeGameString(getUserID());              // Sending userID
      while(peekGameComms() == BOUNTY_SHAKE) {
          readGameComms();
      }
      handshakeState = HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE;
    }
    else if(peekGameComms() == HUNTER_SHAKE && !isHunter)
    {
      writeGameString(getUserID());
      while(peekGameComms() == HUNTER_SHAKE) {
        readGameComms();
      }
      handshakeState = HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE;
    }
 */
HandshakeSendRoleState::HandshakeSendRoleState(Player *player) : State(HANDSHAKE_SEND_ROLE_STATE) {
    this->player = player;
    std::vector<const String*> reading;
    std::vector<const String*> writing;
    String userId = player->getUserID();
    if(player->isHunter()) {
        reading.push_back(&BOUNTY_SHAKE);

        String currentMatchId = *player->getCurrentMatchId();

        writing.push_back(&SEND_MATCH_ID);
        writing.push_back(&currentMatchId);
        writing.push_back(&SEND_USER_ID);
        writing.push_back(&userId);
    } else {
        reading.push_back(&HUNTER_SHAKE);

        writing.push_back(&SEND_USER_ID);
        writing.push_back(&userId);
    }
    registerValidMessages(reading);
    registerResponseMessage(writing);
}

HandshakeSendRoleState::~HandshakeSendRoleState() {
    player = nullptr;
}

void HandshakeSendRoleState::onStateMounted(Device *PDN) {
}

void HandshakeSendRoleState::onStateLoop(Device *PDN) {

    String* incomingMessage = waitForValidMessage(PDN);
    if(incomingMessage != nullptr) {
        for(int i = 0; i < responseStringMessages.size(); i++) {
            PDN->writeString(&responseStringMessages[i]);
        }
        transitionToReceiveRoleState = true;
    }

    // if(peekGameComms() == BOUNTY_SHAKE && player->isHunter()) {
    //     writeGameString(player->getCurrentMatchId());
    //     writeGameString(player->getUserID());
    //     while(peekGameComms() == BOUNTY_SHAKE) {
    //         readGameComms();
    //     }
    //     transitionToReceiveState = true;
    // } else if(peekGameComms() == HUNTER_SHAKE && !player->isHunter()) {
    //     writeGameString(player->getUserID());
    //     while(peekGameComms() == HUNTER_SHAKE) {
    //         readGameComms();
    //     }
    //     transitionToReceiveState = true;
    // }
}

void HandshakeSendRoleState::onStateDismounted(Device *PDN) {
    State::onStateDismounted(PDN);
}

bool HandshakeSendRoleState::transitionToReceiveRole() {
    return transitionToReceiveRoleState;
}