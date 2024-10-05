//
// Created by Elli Furedy on 8/30/2024.
//
#pragma once
#include "../player.hpp"
#include "../state-machine/state-machine.hpp"

// STATE - HANDSHAKE
/**
Handshake states:
0 - start timer to delay sending handshake signal for 1000 ms
1 - wait for handshake delay to expire
2 - start timeout timer
3 - begin sending shake message and check for timeout
**/
enum HandshakeStateId {
    HANDSHAKE_INITIATE_STATE = 1,
    HANDSHAKE_RECEIVE_BATTLE_STATE = 2,
    HANDSHAKE_SEND_ROLE_STATE = 3,
    HANDSHAKE_RECEIVED_ROLE_STATE = 4,
    HANDSHAKE_FINAL_ACK_STATE = 5,
    HANDSHAKE_TERMINAL_STATE = 6
};


class HandshakeInitiateState : public State {
public:
    explicit HandshakeInitiateState(Player *player);

    ~HandshakeInitiateState() override;

    void onStateMounted(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToReceiveBattle();

private:
    const int HANDSHAKE_TIMEOUT = 5000;
    bool isHunter = false;
    bool transitionToReceiveBattleState = false;
};


class HandshakeReceiveBattleState : public State {
public:
    HandshakeReceiveBattleState(Player *player);

    ~HandshakeReceiveBattleState();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToSendRole();

private:
    Player *player;
    bool transitionToSendRoleState = false;
};

class HandshakeSendRoleState : public State {
public:
    HandshakeSendRoleState(Player *player);

    ~HandshakeSendRoleState();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToReceiveRole();

private:
    Player *player;
    bool transitionToReceiveRoleState = false;
};

class HandshakeReceiveRoleState : public State {
public:
    HandshakeReceiveRoleState(Player *player);

    ~HandshakeReceiveRoleState();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToFinalAck();

private:
    Player *player;
    bool transitionToFinalAckState = false;
};

class HandshakeFinalAckState : public State {
public:
    HandshakeFinalAckState(Player *player);

    ~HandshakeFinalAckState();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool handshakeSuccessful();

private:
    Player *player;
    bool handshakeSuccessfulFlag = false;
};

class HandshakeTerminalState : public State {
public:
    HandshakeTerminalState();

    bool isTerminalState() override;
};

class HandshakeStateMachine : public StateMachine {
public:
    explicit HandshakeStateMachine(Player *player, Device *PDN);

    ~HandshakeStateMachine() override;

    void populateStateMap() override;

    bool handshakeSuccessful();

private:
    HandshakeInitiateState *createSendState();

    Player *player;
};
