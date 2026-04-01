#include "apps/handshake/handshake.hpp"

HandshakeApp::HandshakeApp(HandshakeWirelessManager* handshakeWirelessManager, SerialIdentifier jack)
    : StateMachine(HANDSHAKE_APP_ID) {
    this->handshakeWirelessManager = handshakeWirelessManager;
    this->jack = jack;
}

HandshakeApp::~HandshakeApp() {
    handshakeWirelessManager = nullptr;

}

void HandshakeApp::onStateMounted(Device *PDN) {
    StateMachine::onStateMounted(PDN);
}

void HandshakeApp::onStateLoop(Device *PDN) {
    StateMachine::onStateLoop(PDN);

    if (currentState->getStateId() == HandshakeStateId::INPUT_SEND_ID_STATE) {
        if (handshakeTimer.expired()) {
            resetApp(PDN);
        } else if (!handshakeTimer.isRunning()) {
            handshakeTimer.setTimer(handshakeTimeout);
        }
    }

    if (currentState->getStateId() == HandshakeStateId::INPUT_CONNECTED_STATE) {
        handshakeTimer.invalidate();
    }
}

void HandshakeApp::onStateDismounted(Device *PDN) {
    StateMachine::onStateDismounted(PDN);
    handshakeTimer.invalidate();
}

void HandshakeApp::populateStateMap() {
    createStateMap();
}

void HandshakeApp::resetApp(Device *PDN) {
    // stateMap index 0 is always the idle state for both state maps
    skipToState(PDN, 0);
    handshakeWirelessManager->removeMacPeer(jack);
    handshakeTimer.invalidate();
}

void HandshakeApp::createStateMap() {
    InputIdleState* idleState = new InputIdleState(handshakeWirelessManager, jack);
    InputSendIdState* sendIdState = new InputSendIdState(handshakeWirelessManager, jack);
    HandshakeConnectedState* connectedState = new HandshakeConnectedState(handshakeWirelessManager, jack, HandshakeStateId::INPUT_CONNECTED_STATE);

    idleState->addTransition(
        new StateTransition(
            std::bind(&InputIdleState::transitionToSendId, idleState),
            sendIdState));
    sendIdState->addTransition(
        new StateTransition(
            std::bind(&InputSendIdState::transitionToConnected, sendIdState),
            connectedState));
    connectedState->addTransition(
        new StateTransition(
            std::bind(&HandshakeConnectedState::transitionToIdle, connectedState),
            idleState));

    stateMap.push_back(idleState);
    stateMap.push_back(sendIdState);
    stateMap.push_back(connectedState);
}
