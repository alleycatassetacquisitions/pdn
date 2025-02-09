#include "game/handshake-machine.hpp"
#include "id-generator.hpp"

//
// Created by Elli Furedy on 9/30/2024.
//

/*
bool initiateHandshake()
{
  if (comms().available() > 0 && Serial1.peek() == BATTLE_MESSAGE)
  {
    comms().read();
    comms().write(BATTLE_MESSAGE);
    return true;
  }

  return false;
}
    bool initiateHandshake() {
      if (gameCommsAvailable()) {
        if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && isHunter) {
          readGameComms();
          writeGameComms(HUNTER_BATTLE_MESSAGE);
          return true;
        } else if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !isHunter) {
          readGameComms();
          writeGameComms(BOUNTY_BATTLE_MESSAGE);
          return true;
        }
      }
 */
HandshakeInitiateState::HandshakeInitiateState(Player *player) : State(HANDSHAKE_INITIATE_STATE) {
    isHunter = player->isHunter();

    if (isHunter) {
        string battleId = generateUUID();
        string hunterId = player->getUserID();
        string message = battleId + " " + hunterId;
        
    } else {
        string message = player->getUserID();
    }
}

HandshakeInitiateState::~HandshakeInitiateState() {
}


void HandshakeInitiateState::onStateMounted(Device *PDN) {
    for (int i = 0; i < responseStringMessages.size(); i++) {
        PDN->writeString(&responseStringMessages[i]);
    }

    transitionToReceiveBattleState = true;
}

void HandshakeInitiateState::onStateDismounted(Device *PDN) {
    transitionToReceiveBattleState = false;
}

bool HandshakeInitiateState::transitionToReceiveBattle() {
    return transitionToReceiveBattleState;
}
