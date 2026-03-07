#include "device/remote-device-coordinator.hpp"
#include "apps/handshake/handshake-states.hpp"
#include "apps/handshake/handshake.hpp"
#include "device/device-constants.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/serial-manager.hpp"
#include "device/drivers/logger.hpp"
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

    serialManager->setOnStringReceivedCallback(
        [this](std::string msg) { routeSerialMessage(msg, SerialIdentifier::OUTPUT_JACK); },
        SerialIdentifier::OUTPUT_JACK);
    serialManager->setOnStringReceivedCallback(
        [this](std::string msg) { routeSerialMessage(msg, SerialIdentifier::INPUT_JACK); },
        SerialIdentifier::INPUT_JACK);

    handshakeWirelessManager.initialize(wirelessManager);

    inputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::INPUT_JACK);
    outputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::OUTPUT_JACK);

    inputPortHandshake->initialize(PDN);
    outputPortHandshake->initialize(PDN);

    syncLogTimer.setTimer(0);

    wirelessManager->setEspNowPacketHandler(
        PktType::kHandshakeCommand,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<HandshakeWirelessManager*>(ctx)->processHandshakeCommand(macAddress, data, dataLen);
        },
        &handshakeWirelessManager
    );
}

void RemoteDeviceCoordinator::registerSerialHandler(const std::string& prefix, SerialIdentifier jack,
                                                    std::function<void(const std::string&)> handler) {
    auto& handlers = (jack == SerialIdentifier::OUTPUT_JACK) ? outputSerialHandlers_ : inputSerialHandlers_;
    handlers[prefix] = std::move(handler);
}

void RemoteDeviceCoordinator::unregisterSerialHandler(const std::string& prefix, SerialIdentifier jack) {
    auto& handlers = (jack == SerialIdentifier::OUTPUT_JACK) ? outputSerialHandlers_ : inputSerialHandlers_;
    handlers.erase(prefix);
}

void RemoteDeviceCoordinator::routeSerialMessage(const std::string& msg, SerialIdentifier jack) {
    auto& handlers = (jack == SerialIdentifier::OUTPUT_JACK) ? outputSerialHandlers_ : inputSerialHandlers_;
    for (auto& [prefix, handler] : handlers) {
        if (msg.size() >= prefix.size() && msg.substr(0, prefix.size()) == prefix) {
            handler(msg);
            return;
        }
    }
}

void RemoteDeviceCoordinator::sync(Device* PDN) {
    inputPortHandshake->onStateLoop(PDN);
    outputPortHandshake->onStateLoop(PDN);

    if (syncLogTimer.expired()) {
        LOG_W("RDC", "OUTPUT port: status=%d hasPeer=%d | INPUT port: status=%d hasPeer=%d",
              static_cast<int>(getPortStatus(SerialIdentifier::OUTPUT_JACK)),
              getPeerMac(SerialIdentifier::OUTPUT_JACK) != nullptr,
              static_cast<int>(getPortStatus(SerialIdentifier::INPUT_JACK)),
              getPeerMac(SerialIdentifier::INPUT_JACK) != nullptr);

        syncLogTimer.setTimer(100);
    }
}

PortStatus RemoteDeviceCoordinator::getPortStatus(SerialIdentifier port) {
    PortStatus statusByState = mapHandshakeStateToStatus(port);

    //TODO: check the state of the device's other port and determine if it is connected to identify daisy chain!

    return statusByState;
}

PortState RemoteDeviceCoordinator::getPortState(SerialIdentifier port) {
    std::vector<std::array<uint8_t, 6>> peerAddresses;

    const Peer* macPeer = handshakeWirelessManager.getMacPeer(port);
    
    if (macPeer != nullptr) {
        peerAddresses.push_back(macPeer->macAddr);
    }

    return PortState{ port, getPortStatus(port), peerAddresses };
}

PortStatus RemoteDeviceCoordinator::mapHandshakeStateToStatus(SerialIdentifier port) {
    HandshakeApp* app = (port == SerialIdentifier::INPUT_JACK) ? inputPortHandshake : outputPortHandshake;

    if (!app || !app->getCurrentState()) {
        return PortStatus::DISCONNECTED;
    }

    int stateId = app->getCurrentState()->getStateId();

    switch (stateId) {
        case HandshakeStateId::OUTPUT_CONNECTED_STATE:
        case HandshakeStateId::INPUT_CONNECTED_STATE:
            return PortStatus::CONNECTED;

        case HandshakeStateId::OUTPUT_SEND_ID_STATE:
        case HandshakeStateId::INPUT_SEND_ID_STATE:
            return PortStatus::CONNECTING;

        case HandshakeStateId::OUTPUT_IDLE_STATE:
        case HandshakeStateId::INPUT_IDLE_STATE:
        default:
            return PortStatus::DISCONNECTED;
    }
}

const uint8_t* RemoteDeviceCoordinator::getPeerMac(SerialIdentifier port) const {
    const Peer* peer = handshakeWirelessManager.getMacPeer(port);
    return peer ? peer->macAddr.data() : nullptr;
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

