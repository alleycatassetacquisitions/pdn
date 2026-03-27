#include "apps/handshake/handshake-states.hpp"
#include "device/device.hpp"
#include "device/device-constants.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/device-type.hpp"

#define TAG "INPUT_IDLE_STATE"

InputIdleState::InputIdleState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::INPUT_IDLE_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

InputIdleState::~InputIdleState() {
    handshakeWirelessManager = nullptr;
}

void InputIdleState::onStateMounted(Device *PDN) {
    handshakeWirelessManager->setPacketReceivedCallback(std::bind(&InputIdleState::onHandshakeCommandReceived, this, std::placeholders::_1), SerialIdentifier::INPUT_JACK);

    emitMacTimer.setTimer(emitMacInterval);
}

void InputIdleState::onStateLoop(Device *PDN) {
    if (emitMacTimer.expired()) {
        PDN->getSerialManager()->writeString(
            SEND_MAC_ADDRESS + MacToString(PDN->getWirelessManager()->getMacAddress()) +
            PORT_SEPARATOR + std::to_string((int)SerialIdentifier::INPUT_JACK) +
            DEVICE_TYPE_SEPARATOR + std::to_string((int)PDN->getDeviceType()),
            SerialIdentifier::INPUT_JACK);
        emitMacTimer.setTimer(emitMacInterval);
    }
}

void InputIdleState::onStateDismounted(Device *PDN) {
    emitMacTimer.invalidate();
    transitionToSendIdState = false;
    handshakeWirelessManager->clearCallback(SerialIdentifier::INPUT_JACK);
}

void InputIdleState::onHandshakeCommandReceived(HandshakeCommand command) {
    if (command.command == HSCommand::EXCHANGE_ID) {
        Peer peer;
        memcpy(peer.macAddr.data(), command.wifiMacAddr, 6);
        peer.sid = command.sendingJack;
        peer.deviceType = static_cast<DeviceType>(command.deviceType);
        handshakeWirelessManager->setMacPeer(SerialIdentifier::INPUT_JACK, peer);
        transitionToSendIdState = true;
    }
}

bool InputIdleState::transitionToSendId() {
    return transitionToSendIdState;
}
