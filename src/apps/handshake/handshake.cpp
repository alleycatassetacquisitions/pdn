#include "apps/handshake/handshake.hpp"

HandshakeApp::HandshakeApp(HandshakeWirelessManager* handshakeWirelessManager, SerialIdentifier jack)
    : StateMachine(HANDSHAKE_APP_ID) {
    this->handshakeWirelessManager = handshakeWirelessManager;
    this->jack = jack;
}

HandshakeApp::~HandshakeApp() {
    handshakeWirelessManager = nullptr;

}

void HandshakeApp::onStateLoop(Device *PDN) {
    StateMachine::onStateLoop(PDN);

    const int sendIdStateId = (jack == SerialIdentifier::OUTPUT_JACK)
        ? HandshakeStateId::PRIMARY_SEND_ID_STATE
        : HandshakeStateId::AUXILIARY_SEND_ID_STATE;

    const int connectedStateId = (jack == SerialIdentifier::OUTPUT_JACK)
        ? HandshakeStateId::PRIMARY_CONNECTED_STATE
        : HandshakeStateId::AUXILIARY_CONNECTED_STATE;

    if (currentState->getStateId() == sendIdStateId) {
        if (handshakeTimer.expired()) {
            resetApp(PDN);
        } else if (!handshakeTimer.isRunning()) {
            handshakeTimer.setTimer(handshakeTimeout);
        }
    }

    if (currentState->getStateId() == connectedStateId) {
        handshakeTimer.invalidate();
    }
}

void HandshakeApp::onStateDismounted(Device *PDN) {
    StateMachine::onStateDismounted(PDN);
    handshakeTimer.invalidate();
}

void HandshakeApp::populateStateMap() {
    if (jack == SerialIdentifier::OUTPUT_JACK) {
        createOutputJackStateMap();
    } else {
        createInputJackStateMap();
    }
}

void HandshakeApp::resetApp(Device *PDN) {
    // stateMap index 0 is always the idle state for both state maps
    skipToState(PDN, 0);
    handshakeWirelessManager->removeMacPeer(jack);
    handshakeTimer.invalidate();
}

void HandshakeApp::createOutputJackStateMap() {
    PrimaryIdleState* primaryIdleState = new PrimaryIdleState(handshakeWirelessManager);
    PrimarySendIdState* primarySendIdState = new PrimarySendIdState(handshakeWirelessManager);
    HandshakeConnectedState* connectedState = new HandshakeConnectedState(handshakeWirelessManager, SerialIdentifier::OUTPUT_JACK, HandshakeStateId::PRIMARY_CONNECTED_STATE);

    primaryIdleState->addTransition(
        new StateTransition(
            std::bind(&PrimaryIdleState::transitionToPrimarySendId, primaryIdleState),
            primarySendIdState));
    primarySendIdState->addTransition(
        new StateTransition(
            std::bind(&PrimarySendIdState::transitionToConnected, primarySendIdState),
            connectedState));
    connectedState->addTransition(
        new StateTransition(
            std::bind(&HandshakeConnectedState::transitionToIdle, connectedState),
            primaryIdleState));

    stateMap.push_back(primaryIdleState);
    stateMap.push_back(primarySendIdState);
    stateMap.push_back(connectedState);
}

void HandshakeApp::createInputJackStateMap() {
    AuxiliaryIdleState* auxiliaryIdleState = new AuxiliaryIdleState(handshakeWirelessManager);
    AuxiliarySendIdState* auxiliarySendIdState = new AuxiliarySendIdState(handshakeWirelessManager);
    HandshakeConnectedState* connectedState = new HandshakeConnectedState(handshakeWirelessManager, SerialIdentifier::INPUT_JACK, HandshakeStateId::AUXILIARY_CONNECTED_STATE);

    auxiliaryIdleState->addTransition(
        new StateTransition(
            std::bind(&AuxiliaryIdleState::transitionToSendId, auxiliaryIdleState),
            auxiliarySendIdState));
    auxiliarySendIdState->addTransition(
        new StateTransition(
            std::bind(&AuxiliarySendIdState::transitionToConnected, auxiliarySendIdState),
            connectedState));
    connectedState->addTransition(
        new StateTransition(
            std::bind(&HandshakeConnectedState::transitionToIdle, connectedState),
            auxiliaryIdleState));

    stateMap.push_back(auxiliaryIdleState);
    stateMap.push_back(auxiliarySendIdState);
    stateMap.push_back(connectedState);
}