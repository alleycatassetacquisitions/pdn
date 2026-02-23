#include "apps/handshake/handshake-states.hpp"
#include "device/device.hpp"
#include "device/device-constants.hpp"

#define TAG "AUXILIARY_IDLE_STATE"

AuxiliaryIdleState::AuxiliaryIdleState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::AUXILIARY_IDLE_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

AuxiliaryIdleState::~AuxiliaryIdleState() {
    handshakeWirelessManager = nullptr;
}

void AuxiliaryIdleState::onStateMounted(Device *PDN) {
    handshakeWirelessManager->setPacketReceivedCallback(std::bind(&AuxiliaryIdleState::onHandshakeCommandReceived, this, std::placeholders::_1), SerialIdentifier::INPUT_JACK);

    emitMacTimer.setTimer(emitMacInterval);
}

void AuxiliaryIdleState::onStateLoop(Device *PDN) {
    if (emitMacTimer.expired()) {
        PDN->getSerialManager()->writeString(SEND_MAC_ADDRESS + MacToString(PDN->getWirelessManager()->getMacAddress()), SerialIdentifier::INPUT_JACK);
        emitMacTimer.setTimer(emitMacInterval);
    }
}

void AuxiliaryIdleState::onStateDismounted(Device *PDN) {
    emitMacTimer.invalidate();
    transitionToSendIdState = false;
    handshakeWirelessManager->clearCallback(SerialIdentifier::INPUT_JACK);
}

void AuxiliaryIdleState::onHandshakeCommandReceived(HandshakeCommand command) {
    if (command.command == HSCommand::EXCHANGE_ID) {
        handshakeWirelessManager->setMacPeer(command.wifiMacAddr, SerialIdentifier::INPUT_JACK);
        transitionToSendIdState = true;
    }
}

bool AuxiliaryIdleState::transitionToSendId() {
    return transitionToSendIdState;
}