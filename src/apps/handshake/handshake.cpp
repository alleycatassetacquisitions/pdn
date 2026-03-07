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

    const int sendIdStateId = (jack == SerialIdentifier::OUTPUT_JACK)
        ? HandshakeStateId::OUTPUT_SEND_ID_STATE
        : HandshakeStateId::INPUT_SEND_ID_STATE;

    const int connectedStateId = (jack == SerialIdentifier::OUTPUT_JACK)
        ? HandshakeStateId::OUTPUT_CONNECTED_STATE
        : HandshakeStateId::INPUT_CONNECTED_STATE;

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
    OutputIdleState* outputIdleState = new OutputIdleState(handshakeWirelessManager);
    OutputSendIdState* outputSendIdState = new OutputSendIdState(handshakeWirelessManager);
    HandshakeConnectedState* connectedState = new HandshakeConnectedState(handshakeWirelessManager, SerialIdentifier::OUTPUT_JACK, HandshakeStateId::OUTPUT_CONNECTED_STATE);

    outputIdleState->addTransition(
        new StateTransition(
            std::bind(&OutputIdleState::transitionToOutputSendId, outputIdleState),
            outputSendIdState));
    outputSendIdState->addTransition(
        new StateTransition(
            std::bind(&OutputSendIdState::transitionToConnected, outputSendIdState),
            connectedState));
    connectedState->addTransition(
        new StateTransition(
            std::bind(&HandshakeConnectedState::transitionToIdle, connectedState),
            outputIdleState));

    stateMap.push_back(outputIdleState);
    stateMap.push_back(outputSendIdState);
    stateMap.push_back(connectedState);
}

void HandshakeApp::createInputJackStateMap() {
    InputIdleState* inputIdleState = new InputIdleState(handshakeWirelessManager);
    InputSendIdState* inputSendIdState = new InputSendIdState(handshakeWirelessManager);
    HandshakeConnectedState* connectedState = new HandshakeConnectedState(handshakeWirelessManager, SerialIdentifier::INPUT_JACK, HandshakeStateId::INPUT_CONNECTED_STATE);

    inputIdleState->addTransition(
        new StateTransition(
            std::bind(&InputIdleState::transitionToSendId, inputIdleState),
            inputSendIdState));
    inputSendIdState->addTransition(
        new StateTransition(
            std::bind(&InputSendIdState::transitionToConnected, inputSendIdState),
            connectedState));
    connectedState->addTransition(
        new StateTransition(
            std::bind(&HandshakeConnectedState::transitionToIdle, connectedState),
            inputIdleState));

    stateMap.push_back(inputIdleState);
    stateMap.push_back(inputSendIdState);
    stateMap.push_back(connectedState);
}
