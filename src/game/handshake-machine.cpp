//
// Created by Elli Furedy on 8/31/2024.
//
#include "../../include/game/handshake-machine.hpp"

HandshakeStateMachine::HandshakeStateMachine(Player *player, Device *PDN) : StateMachine(PDN) {
    this->player = player;
}

HandshakeStateMachine::~HandshakeStateMachine() {
    player = nullptr;
}

bool HandshakeStateMachine::handshakeSuccessful() {
    return getCurrentState()->getStateId() == HANDSHAKE_TERMINAL_STATE;
}

void HandshakeStateMachine::populateStateMap() {
    HandshakeInitiateState *initiateState = new HandshakeInitiateState(this->player);
    BountySendConnectionConfirmedState *bountySendCC = new BountySendConnectionConfirmedState(this->player);
    HunterSendIdState *hunterSendId = new HunterSendIdState(this->player);
    HunterSendFinalAckState *hunterSendAck = new HunterSendFinalAckState(this->player);
    BountySendFinalAckState *bountySendAck = new BountySendFinalAckState(this->player);
    StartingLineState *startingLine = new StartingLineState(this->player);
    HandshakeTerminalState *terminalState = new HandshakeTerminalState();

    initiateState->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToBountySendCC, initiateState),
            bountySendCC));

    initiateState->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToHunterSendId, initiateState),
            hunterSendId));

    bountySendCC->addTransition(
        new StateTransition(
            std::bind(&BountySendConnectionConfirmedState::transitionToBountySendAck, bountySendCC),
            bountySendAck));

    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&HunterSendIdState::transitionToSendAck, hunterSendId),
            hunterSendAck));

    hunterSendAck->addTransition(
        new StateTransition(
            std::bind(&HunterSendFinalAckState::transitionToStartingLine, hunterSendAck),
            startingLine));

    bountySendAck->addTransition(
        new StateTransition(
            std::bind(&BountySendFinalAckState::transitionToStartingLine, bountySendAck),
            startingLine));

    startingLine->addTransition(
        new StateTransition(
            std::bind(&StartingLineState::handshakeSuccessful, startingLine),
            terminalState));

    stateMap.push_back(initiateState);
    stateMap.push_back(bountySendCC);
    stateMap.push_back(hunterSendId);
    stateMap.push_back(hunterSendAck);
    stateMap.push_back(bountySendAck);
    stateMap.push_back(startingLine);
    stateMap.push_back(terminalState);

}
