//
// Created by Elli Furedy on 8/30/2024.
//
#pragma once
#include "../player.hpp"
#include "../state-machine/state-machine.hpp"
#include "../comms.hpp"

// STATE - HANDSHAKE
/**
Handshake states:
0 - start timer to delay sending handshake signal for 1000 ms
1 - wait for handshake delay to expire
2 - start timeout timer
3 - begin sending shake message and check for timeout
**/
enum HandshakeStateId {
    HANDSHAKE_SEND_ROLE_SHAKE_STATE = 1,
    HANDSHAKE_WAIT_ROLE_SHAKE_STATE = 2,
    HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE = 3,
    HANDSHAKE_FINAL_ACK_STATE = 4
};


class HandshakeSendState : public State {

    public:
    explicit HandshakeSendState(Player* player);
    ~HandshakeSendState() override;

    void onStateMounted(Device *PDN) override;
    void onStateDismounted(Device* PDN) override;
    void addTransition(StateTransition *transition) override;

    bool transitionToWait();

private:
    const int HANDSHAKE_TIMEOUT = 5000;
    bool isHunter = false;
    bool transitionToWaiting = false;
};


class HandshakeWaitState : public State {
public:
    HandshakeWaitState(Player* player);
    ~HandshakeWaitState();
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device* PDN) override;
    void addTransition(StateTransition *transition) override;

    bool transitionToReceive();

private:
    Player* player;
    bool transitionToReceiveState = false;
};

class HandshakeReceiveState : public State {
public:
    HandshakeReceiveState(Player* player);
    ~HandshakeReceiveState();
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device* PDN) override;
    void addTransition(StateTransition *transition) override;

    bool transitionToFinalAck();

private:
    Player* player;
    bool transitionToFinalAckState = false;
};

class HandshakeFinalAckState : public State {
public:
    HandshakeFinalAckState(Player* player);
    ~HandshakeFinalAckState();
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool handshakeSuccessful();

private:
    Player* player;
    bool handshakeSuccessfulFlag = false;
};

class HandshakeStateMachine : public StateMachine {

public:
    explicit HandshakeStateMachine(Player* player);
    ~HandshakeStateMachine() override;
    std::vector<State*> populateStateMap() override;
    bool handshakeSuccessful();

private:
    HandshakeSendState* createSendState();

    Player* player;
};
