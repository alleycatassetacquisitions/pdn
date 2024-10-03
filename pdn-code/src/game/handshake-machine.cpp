//
// Created by Elli Furedy on 8/31/2024.
//
#include "../../include/game/handshake-machine.hpp"

#include "../../include/comms_constants.hpp"
#include "../../include/id-generator.hpp"

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
