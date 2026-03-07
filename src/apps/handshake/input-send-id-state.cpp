#include "apps/handshake/handshake-states.hpp"
#include "device/device.hpp"

#define TAG "INPUT_SEND_ID_STATE"

InputSendIdState::InputSendIdState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::INPUT_SEND_ID_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

InputSendIdState::~InputSendIdState() {
    handshakeWirelessManager = nullptr;
}



void InputSendIdState::onStateMounted(Device *PDN) {
    handshakeWirelessManager->setPacketReceivedCallback(
        std::bind(&InputSendIdState::onHandshakeCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);

    handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
}

void InputSendIdState::onHandshakeCommandReceived(HandshakeCommand command) {
    if (command.command == HSCommand::EXCHANGE_ID) {
        transitionToConnectedState = true;
    }
}

void InputSendIdState::onStateLoop(Device *PDN) {}

void InputSendIdState::onStateDismounted(Device *PDN) {
    transitionToConnectedState = false;
    handshakeWirelessManager->clearCallback(SerialIdentifier::INPUT_JACK);
}


bool InputSendIdState::transitionToConnected() {
    return transitionToConnectedState;
}
