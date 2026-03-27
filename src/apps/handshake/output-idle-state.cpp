#include "apps/handshake/handshake-states.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/wireless-manager.hpp"
#include "device/device.hpp"
#include "device/device-type.hpp"
#include <functional>

#define TAG "OUTPUT_IDLE_STATE"

OutputIdleState::OutputIdleState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::OUTPUT_IDLE_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

OutputIdleState::~OutputIdleState() {
    handshakeWirelessManager = nullptr;
}

void OutputIdleState::onStateMounted(Device *PDN) {
    LOG_I(TAG, "State mounted");

    PDN->getSerialManager()->setOnStringReceivedCallback(std::bind(&OutputIdleState::onConnectionStarted, this, std::placeholders::_1), SerialIdentifier::OUTPUT_JACK);
}

void OutputIdleState::onStateLoop(Device *PDN) {
    
}

void OutputIdleState::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted");
    transitionToOutputSendIdState = false;
    PDN->getSerialManager()->clearCallback(SerialIdentifier::OUTPUT_JACK);
}

void OutputIdleState::onConnectionStarted(std::string remoteMac) {
    if(remoteMac.rfind(SEND_MAC_ADDRESS, 0) == 0) {
        std::string payload = remoteMac.substr(SEND_MAC_ADDRESS.length());
        size_t portSeparatorIndex = payload.rfind('#');
        size_t deviceTypeSeparatorIndex = payload.rfind('t');

        char portChar = payload[portSeparatorIndex + 1];
        int portNumber = portChar - '0';
        int deviceType = std::stoi(payload.substr(deviceTypeSeparatorIndex + 1));

        SerialIdentifier serialPort = static_cast<SerialIdentifier>(portNumber);
        std::string mac = payload.substr(0, portSeparatorIndex);
        uint8_t macBytes[6];
        if (!StringToMac(mac.c_str(), macBytes)) {
            LOG_E(TAG, "Failed to parse MAC address from serial: %s", mac.c_str());
            return;
        }
        Peer peer;
        std::copy(macBytes, macBytes + 6, peer.macAddr.begin());
        peer.sid = serialPort;
        peer.deviceType = static_cast<DeviceType>(deviceType);
        handshakeWirelessManager->setMacPeer(SerialIdentifier::OUTPUT_JACK, peer);
        LOG_I(TAG, "Connection started with remote MAC: %s on port: %d", mac.c_str(), portNumber);
        transitionToOutputSendIdState = true;
    }
}

bool OutputIdleState::transitionToOutputSendId() {
    return transitionToOutputSendIdState;
} 
