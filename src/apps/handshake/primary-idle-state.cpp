#include "apps/handshake/handshake-states.hpp"
#include "device/wireless-manager.hpp"
#include "device/device.hpp"
#include <functional>

#define TAG "PRIMARY_IDLE_STATE"

PrimaryIdleState::PrimaryIdleState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::PRIMARY_IDLE_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

PrimaryIdleState::~PrimaryIdleState() {
    handshakeWirelessManager = nullptr;
}

void PrimaryIdleState::onStateMounted(Device *PDN) {
    LOG_I(TAG, "State mounted");

    PDN->getSerialManager()->setOnStringReceivedCallback(std::bind(&PrimaryIdleState::onConnectionStarted, this, std::placeholders::_1), SerialIdentifier::OUTPUT_JACK);
}

void PrimaryIdleState::onStateLoop(Device *PDN) {
    
}

void PrimaryIdleState::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted");
    transitionToPrimarySendIDState = false;
    PDN->getSerialManager()->clearCallback(SerialIdentifier::OUTPUT_JACK);
}

void PrimaryIdleState::onConnectionStarted(std::string remoteMac) {
    if(remoteMac.starts_with(SEND_MAC_ADDRESS)) {
        std::string mac = remoteMac.substr(SEND_MAC_ADDRESS.length());
        handshakeWirelessManager->setMacPeer(mac, SerialIdentifier::OUTPUT_JACK);
        LOG_I(TAG, "Connection started with remote MAC: %s", mac.c_str());
        transitionToPrimarySendIDState = true;
    }
}

bool PrimaryIdleState::transitionToPrimarySendId() {
    return transitionToPrimarySendIDState;
} 
