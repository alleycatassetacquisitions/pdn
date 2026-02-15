#include "../../include/apps/handshake.hpp"

HandshakeApp::HandshakeApp(Player* player, MatchManager* matchManager,
                           QuickdrawWirelessManager* quickdrawWirelessManager)
    : StateMachine(HANDSHAKE_APP_ID) {
    this->player = player;
    this->matchManager = matchManager;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
}

HandshakeApp::~HandshakeApp() {
    player = nullptr;
    matchManager = nullptr;
    quickdrawWirelessManager = nullptr;
}

void HandshakeApp::populateStateMap() {
    // Instantiate handshake states using existing classes
    HandshakeInitiateState* initiate = new HandshakeInitiateState(player);
    BountySendConnectionConfirmedState* bountySendCC = new BountySendConnectionConfirmedState(
        player, matchManager, quickdrawWirelessManager);
    HunterSendIdState* hunterSendId = new HunterSendIdState(
        player, matchManager, quickdrawWirelessManager);
    ConnectionSuccessful* connectionSuccessful = new ConnectionSuccessful(player);

    // Wire handshake transitions - copied from quickdraw.cpp

    // HandshakeInitiate → BountySendCC (for Bounty role)
    initiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToBountySendCC, initiate),
            bountySendCC));

    // HandshakeInitiate → HunterSendId (for Hunter role)
    initiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToHunterSendId, initiate),
            hunterSendId));

    // HandshakeInitiate → timeout (sets flag)
    initiate->addTransition(
        new StateTransition(
            [this, initiate]() {
                if (initiate->transitionToIdle()) {
                    this->handshakeTimedOut = true;
                    return true;
                }
                return false;
            },
            nullptr));  // Terminal transition - no target state

    // BountySendCC → ConnectionSuccessful
    bountySendCC->addTransition(
        new StateTransition(
            std::bind(&BountySendConnectionConfirmedState::transitionToConnectionSuccessful, bountySendCC),
            connectionSuccessful));

    // BountySendCC → timeout (sets flag)
    bountySendCC->addTransition(
        new StateTransition(
            [this, bountySendCC]() {
                if (bountySendCC->transitionToIdle()) {
                    this->handshakeTimedOut = true;
                    return true;
                }
                return false;
            },
            nullptr));  // Terminal transition

    // HunterSendId → ConnectionSuccessful
    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&HunterSendIdState::transitionToConnectionSuccessful, hunterSendId),
            connectionSuccessful));

    // HunterSendId → timeout (sets flag)
    hunterSendId->addTransition(
        new StateTransition(
            [this, hunterSendId]() {
                if (hunterSendId->transitionToIdle()) {
                    this->handshakeTimedOut = true;
                    return true;
                }
                return false;
            },
            nullptr));  // Terminal transition

    // ConnectionSuccessful → terminal (sets completion flag)
    connectionSuccessful->addTransition(
        new StateTransition(
            [this, connectionSuccessful]() {
                if (connectionSuccessful->transitionToCountdown()) {
                    this->handshakeComplete = true;
                    return true;
                }
                return false;
            },
            nullptr));  // Terminal transition - parent will take over

    // Push all states to state map
    stateMap.push_back(initiate);
    stateMap.push_back(bountySendCC);
    stateMap.push_back(hunterSendId);
    stateMap.push_back(connectionSuccessful);

    // Set initial state
    currentState = initiate;
}

bool HandshakeApp::readyForCountdown() {
    return handshakeComplete;
}

bool HandshakeApp::timedOut() {
    return handshakeTimedOut;
}
