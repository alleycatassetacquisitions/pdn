#include "id-generator.hpp"
#include "game/handshake-machine.hpp"
#include "comms_constants.hpp"
//
// Created by Elli Furedy on 10/1/2024.
//
/*
    if (isHunter) {
          writeGameComms(HUNTER_SHAKE);
        } else {
          writeGameComms(BOUNTY_SHAKE);
        }

        handshakeState = HANDSHAKE_WAIT_ROLE_SHAKE_STATE;


        ...


    if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !isHunter)
        {
          //it's confused.
          while(peekGameComms() == HUNTER_BATTLE_MESSAGE) {
            readGameComms();
          }
          writeGameComms(BOUNTY_BATTLE_MESSAGE);
          writeGameComms(BOUNTY_SHAKE);
        }

        else if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && isHunter)
        {
          //also confused.
          while(peekGameComms() == BOUNTY_BATTLE_MESSAGE) {
            readGameComms();
          }
          writeGameComms(HUNTER_BATTLE_MESSAGE);
          writeGameComms(HUNTER_SHAKE);
        }
 */
HandshakeReceiveBattleState::HandshakeReceiveBattleState(Player *player) : State(HANDSHAKE_RECEIVE_BATTLE_STATE) {
    this->player = player;
    std::vector<const String *> reading;
    std::vector<const String *> writing;
    if (player->isHunter()) {
        reading.push_back(&BOUNTY_BATTLE_MESSAGE);

        writing.push_back(&HUNTER_BATTLE_MESSAGE);
        writing.push_back(&HUNTER_SHAKE);
    } else {
        reading.push_back(&HUNTER_BATTLE_MESSAGE);

        writing.push_back(&BOUNTY_BATTLE_MESSAGE);
        writing.push_back(&BOUNTY_SHAKE);
    }
    registerValidMessages(reading);
    registerResponseMessage(writing);
}

HandshakeReceiveBattleState::~HandshakeReceiveBattleState() {
    player = nullptr;
}

void HandshakeReceiveBattleState::onStateMounted(Device *PDN) {
    if (player->isHunter()) {
        player->setCurrentMatchId(IdGenerator::GetInstance()->generateId());
    }
}

void HandshakeReceiveBattleState::onStateLoop(Device *PDN) {
    String *incomingMessage = waitForValidMessage(PDN);
    if (incomingMessage != nullptr) {
        for (int i = 0; i < responseStringMessages.size(); i++) {
            PDN->writeString(&responseStringMessages[i]);
        }
        transitionToSendRoleState = true;
    }
}

void HandshakeReceiveBattleState::onStateDismounted(Device *PDN) {
    transitionToSendRoleState = false;
}

bool HandshakeReceiveBattleState::transitionToSendRole() {
    return transitionToSendRoleState;
}
