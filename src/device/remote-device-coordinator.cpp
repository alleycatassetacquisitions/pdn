#include "device/remote-device-coordinator.hpp"
#include "apps/handshake/handshake-states.hpp"
#include "apps/handshake/handshake.hpp"
#include "device/device-constants.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/serial-manager.hpp"
#include "state/state-machine.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "wireless/peer-comms-types.hpp"

RemoteDeviceCoordinator::RemoteDeviceCoordinator() : handshakeWirelessManager(HandshakeWirelessManager()) {}

RemoteDeviceCoordinator::~RemoteDeviceCoordinator() {
    delete inputPortHandshake;
    delete outputPortHandshake;
}


void RemoteDeviceCoordinator::initialize(WirelessManager* wirelessManager, SerialManager* serialManager, Device* PDN) {
    this->serialManager = serialManager;

    // TODO: this is not right lol
    handshakeWirelessManager.initialize(wirelessManager);

    // TODO: determine input vs output
    inputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::INPUT_JACK);
    outputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::OUTPUT_JACK);

    inputPortHandshake->initialize(PDN);
    outputPortHandshake->initialize(PDN);

    // TODO: register the handshakeWirelessManager's packet received callback.
    wirelessManager->setEspNowPacketHandler(
        PktType::kHandshakeCommand, 
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<HandshakeWirelessManager*>(ctx)->processHandshakeCommand(macAddress, data, dataLen);
        },
        &handshakeWirelessManager
    );
}

void RemoteDeviceCoordinator::sync(Device* PDN) {
    inputPortHandshake->onStateLoop(PDN);
    outputPortHandshake->onStateLoop(PDN);
}

PortStatus RemoteDeviceCoordinator::getPortStatus(SerialIdentifier port) {
    return mapHandshakeStateToStatus(port);

    PortStatus statusByState = mapHandshakeStateToStatus(port);

    //TODO: check the state of the device's other port and determine if it is connected to identify daisy chain!

    return statusByState;
}

PortState RemoteDeviceCoordinator::getPortState(SerialIdentifier port) {
    std::vector<std::array<uint8_t, 6>> peerAddresses;

    const std::array<uint8_t, 6>* macPeer = handshakeWirelessManager.getMacPeer(port);
    
    if (macPeer != nullptr) {
        peerAddresses.push_back(*macPeer);
    }
    
    PortState ps = { 
        port, 
        getPortStatus(port), 
        peerAddresses  
    }; 

    return ps;
}

PortStatus RemoteDeviceCoordinator::mapHandshakeStateToStatus(SerialIdentifier port) {
    int stateId;

    if (port == SerialIdentifier::INPUT_JACK) { stateId = inputPortHandshake->getCurrentState()->getStateId(); }
    else { stateId = outputPortHandshake->getCurrentState()->getStateId(); }

    switch (stateId) {
        case HandshakeStateId::PRIMARY_CONNECTED_STATE:
        case HandshakeStateId::AUXILIARY_CONNECTED_STATE:
            return PortStatus::CONNECTED;

        case HandshakeStateId::PRIMARY_SEND_ID_STATE:
        case HandshakeStateId::AUXILIARY_SEND_ID_STATE:
            return PortStatus::CONNECTING;

        case HandshakeStateId::PRIMARY_IDLE_STATE:
        case HandshakeStateId::AUXILIARY_IDLE_STATE:
        default:
            return PortStatus::DISCONNECTED;
    }
}

void RemoteDeviceCoordinator::addDaisyChainedPeer(const uint8_t* macAddress) {
    //TODO: Implement
}

void RemoteDeviceCoordinator::removeDaisyChainedPeer(const uint8_t* macAddress) {
    //TODO: Implement
}

void RemoteDeviceCoordinator::registerPeer(const uint8_t* macAddress) {
    //TODO: Implement
}

void RemoteDeviceCoordinator::unregisterPeer(const uint8_t* macAddress) {
    //TODO: Implement
}

