#include "apps/handshake/handshake-states.hpp"
#include "device/device.hpp"

#define TAG "OUTPUT_SEND_ID_STATE"

OutputSendIdState::OutputSendIdState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::OUTPUT_SEND_ID_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

OutputSendIdState::~OutputSendIdState() {
    handshakeWirelessManager = nullptr;
}


void OutputSendIdState::onStateMounted(Device *PDN) {
    handshakeWirelessManager->setPacketReceivedCallback(std::bind(&OutputSendIdState::onHandshakeCommandReceived, this, std::placeholders::_1), SerialIdentifier::OUTPUT_JACK);

    handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
}

void OutputSendIdState::onHandshakeCommandReceived(HandshakeCommand command) {
    if (command.command == HSCommand::EXCHANGE_ID) {
        handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
        transitionToConnectionSuccessfulState = true;
    }
}


void OutputSendIdState::onStateLoop(Device *PDN) {}

void OutputSendIdState::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted");
    transitionToConnectionSuccessfulState = false;
    handshakeWirelessManager->clearCallback(SerialIdentifier::OUTPUT_JACK);
}

bool OutputSendIdState::transitionToConnected() {
    return transitionToConnectionSuccessfulState;
}
