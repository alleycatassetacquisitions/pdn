//
// Created by Elli Furedy on 8/30/2024.
//
#pragma once
#include "../../lib/pdn-libs/player.hpp"
#include "state-machine.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"


// STATE - HANDSHAKE
/**
 * 
 * TODO: Create new init state that routes to correct handshake state based on player role.
Handshake states:
0 - Send battle message once.
1 - Wait for battle message, once received send battle_message & shake.
2 - Hunter - send matchId & playerId. Bounty - send player id.
3 - Read matchId/playerId, send final ack.
4 - Wait for receive final ack.
5 - Terminal state to signal success to outer state-machine.
**/
enum HandshakeStateId {
    HANDSHAKE_INITIATE_STATE = 0,
    BOUNTY_SEND_CC_STATE = 1,
    HUNTER_SEND_ID_STATE = 2,
    BOUNTY_SEND_FINAL_ACK_STATE = 3,
    HUNTER_SEND_FINAL_ACK_STATE = 4,
    HANDSHAKE_STARTING_LINE_STATE = 5,
    HANDSHAKE_TERMINAL_STATE = 6

};


class HandshakeInitiateState : public State {
public:
    HandshakeInitiateState(Player *player);

    ~HandshakeInitiateState();

    bool transitionToBountySendCC();

    bool transitionToHunterSendId();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    private:
    SimpleTimer handshakeSettlingTimer;
    const int HANDSHAKE_SETTLE_TIME = 500;
    Player *player;
    bool transitionToBountySendCCState = false;
    bool transitionToHunterSendIdState = false;
};

class BountySendConnectionConfirmedState : public State {
public:

    explicit BountySendConnectionConfirmedState(Player *player);

    ~BountySendConnectionConfirmedState() override;

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToBountySendAck();

private:
    SimpleTimer delayTimer;
    const int delay = 100;
    Player *player;
    bool transitionToBountySendAckState = false;

};


class HunterSendIdState : public State {
public:
    HunterSendIdState(Player *player);

    ~HunterSendIdState();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToSendAck();

    void onQuickdrawCommandReceived(QuickdrawCommand command);

private:
    Player *player;
    bool transitionToHunterSendAckState = false;
};

class BountySendFinalAckState : public State {
public:
    BountySendFinalAckState(Player *player);

    ~BountySendFinalAckState();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    void onQuickdrawCommandReceived(QuickdrawCommand command);


    bool transitionToStartingLine();

private:
    Player *player;
    bool transitionToStartingLineState = false;
};

class HunterSendFinalAckState : public State {
public:
    HunterSendFinalAckState(Player *player);

    ~HunterSendFinalAckState();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    void onQuickdrawCommandReceived(QuickdrawCommand command);

    bool transitionToStartingLine();



private:
    Player *player;
    bool transitionToStartingLineState = false;
};



class StartingLineState : public State {
public:
    StartingLineState(Player *player);

    ~StartingLineState();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    void onQuickdrawCommandReceived(QuickdrawCommand command);

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
    BountySendConnectionConfirmedState *createSendState();

    Player *player;
};
