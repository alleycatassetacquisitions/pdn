#include "apps/handshake/handshake.hpp"

HandshakeApp::HandshakeApp(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager)
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

void HandshakeApp::onStateMounted(Device *PDN) {
    // Initialize the state machine (populates state map and mounts first state)
    StateMachine::onStateMounted(PDN);
    // Initialize the timeout timer
    initTimeout();
}

void HandshakeApp::onStateDismounted(Device *PDN) {
    // Reset the timeout timer
    resetTimeout();
    // Clean up the state machine
    StateMachine::onStateDismounted(PDN);
}

void HandshakeApp::populateStateMap() {
    HandshakeInitiateState* handshakeInitiate = new HandshakeInitiateState(player);
    BountySendConnectionConfirmedState* bountySendCC = new BountySendConnectionConfirmedState(player, matchManager, quickdrawWirelessManager);
    HunterSendIdState* hunterSendId = new HunterSendIdState(player, matchManager, quickdrawWirelessManager);
    ConnectionSuccessful* connectionSuccessfulState = new ConnectionSuccessful(player);

    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToBountySendCC, handshakeInitiate),
            bountySendCC));

    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToHunterSendId, handshakeInitiate),
            hunterSendId));

    // NOTE: BaseHandshakeState::transitionToIdle (timeout) is NOT wired internally.
    // It is an exit condition checked by the parent state machine via didTimeout().

    bountySendCC->addTransition(
        new StateTransition(
            std::bind(&BountySendConnectionConfirmedState::transitionToConnectionSuccessful, bountySendCC),
            connectionSuccessfulState));

    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&HunterSendIdState::transitionToConnectionSuccessful, hunterSendId),
            connectionSuccessfulState));

    // NOTE: ConnectionSuccessful::transitionToCountdown is NOT wired internally.
    // It is an exit condition checked by the parent state machine via readyForCountdown().

    stateMap.push_back(handshakeInitiate);
    stateMap.push_back(bountySendCC);
    stateMap.push_back(hunterSendId);
    stateMap.push_back(connectionSuccessfulState);
}


