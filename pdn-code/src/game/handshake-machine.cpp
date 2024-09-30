//
// Created by Elli Furedy on 8/31/2024.
//
#include "../include/game/handshake-machine.hpp"

#include "comms_constants.hpp"
#include "id-generator.hpp"

HandshakeInitiateState::HandshakeInitiateState(Player* player) : State(HANDSHAKE_INITIATE_STATE) {
    isHunter = player->isHunter();
    std::vector<const String*> writing;

    if(isHunter) {
        writing.push_back(&HUNTER_BATTLE_MESSAGE);
    } else {
        writing.push_back(&BOUNTY_BATTLE_MESSAGE);
    }

    registerResponseMessage(writing);
}

HandshakeInitiateState::~HandshakeInitiateState() {
}

void HandshakeInitiateState::onStateMounted(Device *PDN) {
    for(int i = 0; i < responseStringMessages.size(); i++) {
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

HandshakeReceiveBattleState::HandshakeReceiveBattleState(Player* player) : State(HANDSHAKE_RECEIVE_BATTLE_STATE) {
    this->player = player;
    std::vector<const String*> reading;
    std::vector<const String*> writing;
    if(player->isHunter()) {
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
    if(player->isHunter()) {
        player->setCurrentMatchId(IdGenerator::GetInstance()->generateId());
    }

}

void HandshakeReceiveBattleState::onStateLoop(Device *PDN) {

    String* incomingMessage = waitForValidMessage(PDN);
    if(incomingMessage != nullptr) {
        for(int i = 0; i < responseStringMessages.size(); i++) {
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

HandshakeReceiveRoleState::HandshakeReceiveRoleState(Player* player) : State(HANDSHAKE_RECEIVED_ROLE_STATE) {
    this->player = player;
    std::vector<const String*> writing;
    std::vector<const String*> reading;
    if(player->isHunter()) {
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

void HandshakeReceiveRoleState::onStateMounted(Device *PDN) {}

void HandshakeReceiveRoleState::onStateLoop(Device *PDN) {

    String* incomingMessage = waitForValidMessage(PDN);
    if(incomingMessage != nullptr) {
        if(*incomingMessage == SEND_USER_ID) {
            String userId = PDN->readString();
            player->setCurrentOpponentId(userId);
        }
        if(*incomingMessage == SEND_MATCH_ID) {
            String matchId = PDN->readString();
            player->setCurrentMatchId(matchId);
        }
    }

    if(player->getCurrentMatchId() != nullptr && player->getCurrentOpponentId() != nullptr) {
        for(int i = 0; i < responseStringMessages.size(); i++) {
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

HandshakeFinalAckState::HandshakeFinalAckState(Player* player) : State(HANDSHAKE_FINAL_ACK_STATE) {
    this->player = player;
    std::vector<const String*> reading;
    if(player->isHunter()) {
        reading.push_back(&BOUNTY_HANDSHAKE_FINAL_ACK);
    } else {
        reading.push_back(&HUNTER_HANDSHAKE_FINAL_ACK);
    }
}

HandshakeFinalAckState::~HandshakeFinalAckState() {
    player = nullptr;
}

void HandshakeFinalAckState::onStateMounted(Device *PDN) {

}


void HandshakeFinalAckState::onStateLoop(Device *PDN) {
    String* incomingMessage = waitForValidMessage(PDN);
    if(incomingMessage != nullptr) {
        handshakeSuccessfulFlag = true;
    }

    // if((player->isHunter() && peekGameComms() == BOUNTY_HANDSHAKE_FINAL_ACK) ||
    //     (!player->isHunter() && peekGameComms() == HUNTER_HANDSHAKE_FINAL_ACK)) {
    //     handshakeSuccessfulFlag = true;
    // }
}

void HandshakeFinalAckState::onStateDismounted(Device *PDN) {
    handshakeSuccessfulFlag = false;
}

bool HandshakeFinalAckState::handshakeSuccessful() {
    return handshakeSuccessfulFlag;
}

HandshakeTerminalState::HandshakeTerminalState() : State(HANDSHAKE_TERMINAL_STATE){}

bool HandshakeStateMachine::handshakeSuccessful() {
    return getCurrentState()->getName() == HANDSHAKE_TERMINAL_STATE;
}

HandshakeStateMachine::HandshakeStateMachine(Player* player) {
    this->player = player;
}

HandshakeStateMachine::~HandshakeStateMachine() {
    player = nullptr;
}

void HandshakeStateMachine::populateStateMap() {

    HandshakeInitiateState* initiateState = new HandshakeInitiateState(this->player);
    HandshakeReceiveBattleState* receiveBattleState = new HandshakeReceiveBattleState(this->player);
    HandshakeSendRoleState* sendRoleState = new HandshakeSendRoleState(this->player);
    HandshakeReceiveRoleState* receiveRoleState = new HandshakeReceiveRoleState(this->player);
    HandshakeFinalAckState* finalAckState = new HandshakeFinalAckState(this->player);
    HandshakeTerminalState* terminalState = new HandshakeTerminalState();


    initiateState->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToReceiveBattle,
                initiateState),
                receiveBattleState));

    receiveBattleState->addTransition(
        new StateTransition(
            std::bind(&HandshakeReceiveBattleState::transitionToSendRole,
                receiveBattleState),
                sendRoleState));

    sendRoleState->addTransition(
        new StateTransition(
            std::bind(&HandshakeSendRoleState::transitionToReceiveRole,
            sendRoleState),
            receiveRoleState));

    receiveRoleState->addTransition(
        new StateTransition(
            std::bind(&HandshakeReceiveRoleState::transitionToFinalAck,
                receiveRoleState),
                finalAckState));

    finalAckState->addTransition(
        new StateTransition(
            std::bind(&HandshakeFinalAckState::handshakeSuccessful,
                finalAckState),
                terminalState));

    stateMap.push_back(initiateState);
    stateMap.push_back(receiveBattleState);
    stateMap.push_back(receiveRoleState);
    stateMap.push_back(finalAckState);
    stateMap.push_back(terminalState);
}
