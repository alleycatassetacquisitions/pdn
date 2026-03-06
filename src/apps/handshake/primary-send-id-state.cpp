#include "apps/handshake/handshake-states.hpp"
#include "device/device.hpp"

#define TAG "PRIMARY_SEND_ID_STATE"

PrimarySendIdState::PrimarySendIdState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::PRIMARY_SEND_ID_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

PrimarySendIdState::~PrimarySendIdState() {
    handshakeWirelessManager = nullptr;
}


void PrimarySendIdState::onStateMounted(Device *PDN) {
    handshakeWirelessManager->setPacketReceivedCallback(std::bind(&PrimarySendIdState::onHandshakeCommandReceived, this, std::placeholders::_1), SerialIdentifier::OUTPUT_JACK);

    handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
}

void PrimarySendIdState::onHandshakeCommandReceived(HandshakeCommand command) {
    if (command.command == HSCommand::EXCHANGE_ID) {
        handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
        transitionToConnectionSuccessfulState = true;
    }
}


void PrimarySendIdState::onStateLoop(Device *PDN) {}

void PrimarySendIdState::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted");
    transitionToConnectionSuccessfulState = false;
    handshakeWirelessManager->clearCallback(SerialIdentifier::OUTPUT_JACK);
}

bool PrimarySendIdState::transitionToConnected() {
    return transitionToConnectionSuccessfulState;
}
