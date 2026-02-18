#include "apps/handshake/handshake-states.hpp"
#include "device/device.hpp"

#define TAG "AUXILIARY_SEND_ID_STATE"

AuxiliarySendIdState::AuxiliarySendIdState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::AUXILIARY_SEND_ID_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

AuxiliarySendIdState::~AuxiliarySendIdState() {
    handshakeWirelessManager = nullptr;
}



void AuxiliarySendIdState::onStateMounted(Device *PDN) {
    handshakeWirelessManager->setPacketReceivedCallback(
        std::bind(&AuxiliarySendIdState::onHandshakeCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);

    handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::INPUT_JACK);
}

void AuxiliarySendIdState::onHandshakeCommandReceived(HandshakeCommand command) {
    if (command.command == HSCommand::EXCHANGE_ID) {
        transitionToConnectedState = true;
    }
}

void AuxiliarySendIdState::onStateLoop(Device *PDN) {}

void AuxiliarySendIdState::onStateDismounted(Device *PDN) {
    transitionToConnectedState = false;
    handshakeWirelessManager->clearCallback(SerialIdentifier::INPUT_JACK);
}


bool AuxiliarySendIdState::transitionToConnected() {
    return transitionToConnectedState;
}