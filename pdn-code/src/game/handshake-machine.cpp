//
// Created by Elli Furedy on 8/31/2024.
//
#include "../include/game/handshake-machine.hpp"

#include "comms.hpp"
#include "id-generator.hpp"

HandshakeSendState::HandshakeSendState(Player* player) : State(HANDSHAKE_SEND_ROLE_SHAKE_STATE) {
    isHunter = player->isHunter();
}

HandshakeSendState::~HandshakeSendState() {
}

void HandshakeSendState::onStateMounted(Device *PDN) {
    if(isHunter) {
        writeGameComms(HUNTER_SHAKE);
    } else {
        writeGameComms(BOUNTY_SHAKE);
    }

    transitionToWaiting = true;
}

void HandshakeSendState::onStateDismounted(Device *PDN) {
    transitionToWaiting = false;
}

bool HandshakeSendState::transitionToWait() {
    return transitionToWaiting;
}

void HandshakeSendState::addTransition(StateTransition *transition) {
    State::addTransition(transition);
}

HandshakeWaitState::HandshakeWaitState(Player* player) : State(HANDSHAKE_WAIT_ROLE_SHAKE_STATE) {
    this->player = player;
}

HandshakeWaitState::~HandshakeWaitState() {
    player = nullptr;
}

void HandshakeWaitState::onStateMounted(Device *PDN) {
    player->setCurrentMatchId(new String(IdGenerator::GetInstance()->generateId()));

}

void HandshakeWaitState::onStateLoop(Device *PDN) {
    if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && player->isHunter) {
        while(peekGameComms() == BOUNTY_BATTLE_MESSAGE) {
            readGameComms();
        }
        writeGameComms(HUNTER_BATTLE_MESSAGE);
        writeGameComms(HUNTER_SHAKE);
    } else if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !player->isHunter) {
        while(peekGameComms() == HUNTER_BATTLE_MESSAGE) {
            readGameComms();
        }
        writeGameComms(BOUNTY_BATTLE_MESSAGE);
        writeGameComms(BOUNTY_SHAKE);
    }

    if(peekGameComms() == BOUNTY_SHAKE && player->isHunter) {
        writeGameString(player->getCurrentMatchId());
        writeGameString(player->getUserID());
        while(peekGameComms() == BOUNTY_SHAKE) {
            readGameComms();
        }
        transitionToReceiveState = true;
    } else if(peekGameComms() == HUNTER_SHAKE && !player->isHunter()) {
        writeGameString(player->getUserID());
        while(peekGameComms() == HUNTER_SHAKE) {
            readGameComms();
        }
        transitionToReceiveState = true;
    }
}

void HandshakeWaitState::onStateDismounted(Device *PDN) {
    transitionToReceiveState = false;
}

bool HandshakeWaitState::transitionToReceive() {
    return transitionToReceiveState;
}

void HandshakeWaitState::addTransition(StateTransition *transition) {
    State::addTransition(transition);
}

HandshakeReceiveState::HandshakeReceiveState(Player* player) : State(HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE) {
    this->player = player;
}

HandshakeReceiveState::~HandshakeReceiveState() {
    player = nullptr;
}

void HandshakeReceiveState::onStateLoop(Device *PDN) {
    if(player->isHunter()) {
        if(gameCommsAvailable()) {
            String opponentId = readGameString('\r');
            player->setCurrentOpponentId(&opponentId);
            transitionToFinalAckState = true;
        }
    } else if(!player->isHunter()) {
        if(gameCommsAvailable()) {
            String currentMatchId = readGameString('\r');
            player->setCurrentMatchId(&currentMatchId);
            String opponentId = readGameString('\r');
            player->setCurrentOpponentId(&opponentId);
            transitionToFinalAckState = true;
        }
    }
}

void HandshakeReceiveState::onStateDismounted(Device *PDN) {
    if(player->isHunter()) {
        writeGameComms(HUNTER_HANDSHAKE_FINAL_ACK);
    } else if(!player->isHunter()) {
        writeGameComms(BOUNTY_HANDSHAKE_FINAL_ACK);
    }

    transitionToFinalAckState = false;
}

bool HandshakeReceiveState::transitionToFinalAck() {
    return transitionToFinalAckState;
}

void HandshakeReceiveState::addTransition(StateTransition *transition) {
    State::addTransition(transition);
}

HandshakeFinalAckState::HandshakeFinalAckState(Player* player) : State(HANDSHAKE_FINAL_ACK_STATE) {
    this->player = player;
}

HandshakeFinalAckState::~HandshakeFinalAckState() {
    player = nullptr;
}

void HandshakeFinalAckState::onStateLoop(Device *PDN) {
    if((player->isHunter() && peekGameComms() == BOUNTY_HANDSHAKE_FINAL_ACK) ||
        (!player->isHunter() && peekGameComms() == HUNTER_HANDSHAKE_FINAL_ACK)) {
        handshakeSuccessfulFlag = true;
    }
}

void HandshakeFinalAckState::onStateDismounted(Device *PDN) {
    readGameComms();
    handshakeSuccessfulFlag = false;
}

bool HandshakeFinalAckState::handshakeSuccessful() {
    return handshakeSuccessfulFlag;
}

bool HandshakeStateMachine::handshakeSuccessful() {
    if(getCurrentState()->getName() == HANDSHAKE_FINAL_ACK_STATE) {
        HandshakeFinalAckState* currentState = dynamic_cast<HandshakeFinalAckState*>(getCurrentState());
        if(currentState != nullptr) {
            return currentState->handshakeSuccessful();
        }
    }
}

HandshakeStateMachine::HandshakeStateMachine(Player* player) {
    this->player = player;
}

HandshakeStateMachine::~HandshakeStateMachine() {
    player = nullptr;
}

std::vector<State*> HandshakeStateMachine::populateStateMap() {

    HandshakeSendState* sendState = new HandshakeSendState(this->player);
    HandshakeWaitState* waitState = new HandshakeWaitState(this->player);
    HandshakeReceiveState* receiveState = new HandshakeReceiveState(this->player);
    HandshakeFinalAckState* finalState = new HandshakeFinalAckState(this->player);

    sendState->addTransition(new StateTransition(std::bind(&HandshakeSendState::transitionToWait, sendState), waitState));
    waitState->addTransition(new StateTransition(std::bind(&HandshakeWaitState::transitionToReceive, waitState), receiveState));
    receiveState->addTransition(new StateTransition(std::bind(&HandshakeReceiveState::transitionToFinalAck, receiveState), finalState));

    stateMap.push_back(sendState);
    stateMap.push_back(waitState);
    stateMap.push_back(receiveState);
    stateMap.push_back(finalState);
}
