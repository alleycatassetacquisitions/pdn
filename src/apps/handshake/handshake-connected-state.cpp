#include "apps/handshake/handshake-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"
#include "device/device-type.hpp"

HandshakeConnectedState::HandshakeConnectedState(HandshakeWirelessManager* handshakeWirelessManager, SerialIdentifier jack, int stateId)
    : State(stateId), jack(jack) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

HandshakeConnectedState::~HandshakeConnectedState() {
    handshakeWirelessManager = nullptr;
}

void HandshakeConnectedState::onStateMounted(Device *PDN) {
    PDN->getSerialManager()->setOnStringReceivedCallback(
        std::bind(&HandshakeConnectedState::heartbeatMonitorStringCallback, this, std::placeholders::_1), jack);
    handshakeWirelessManager->setPacketReceivedCallback(
        std::bind(&HandshakeConnectedState::listenForHandshakeCommand, this, std::placeholders::_1), jack);

    emitHeartbeatTimer.setTimer(emitHeartbeatInterval);
    heartbeatMonitorTimer.setTimer(firstHeartbeatTimeout);
}

void HandshakeConnectedState::onStateLoop(Device *PDN) {
    if (heartbeatMonitorTimer.expired()) {
        handshakeWirelessManager->sendPacket(HSCommand::NOTIFY_DISCONNECT, jack);
        heartbeatMonitorTimer.invalidate();
        transitionToIdleState = true;
    }

    if (emitHeartbeatTimer.expired()) {
        PDN->getSerialManager()->writeString(SERIAL_HEARTBEAT, jack);
        emitHeartbeatTimer.setTimer(emitHeartbeatInterval);
    }
}

void HandshakeConnectedState::onStateDismounted(Device *PDN) {
    PDN->getSerialManager()->clearCallback(jack);
    heartbeatMonitorTimer.invalidate();
    emitHeartbeatTimer.invalidate();
    transitionToIdleState = false;
    handshakeWirelessManager->clearCallback(jack);
    handshakeWirelessManager->removeMacPeer(jack);
}

void HandshakeConnectedState::heartbeatMonitorStringCallback(const std::string& message) {
    if (message.rfind(SERIAL_HEARTBEAT, 0) == 0) {
        heartbeatMonitorTimer.invalidate();
        heartbeatMonitorTimer.setTimer(heartbeatMonitorInterval);
    }
}

void HandshakeConnectedState::listenForHandshakeCommand(HandshakeCommand command) {
    if (command.command == HSCommand::NOTIFY_DISCONNECT) {
        transitionToIdleState = true;
    } else if (command.command == HSCommand::EXCHANGE_ID) {
        Peer peer;
        memcpy(peer.macAddr.data(), command.wifiMacAddr, 6);
        peer.sid = command.sendingJack;
        peer.deviceType = static_cast<DeviceType>(command.deviceType);
        handshakeWirelessManager->setMacPeer(jack, peer);
    }
}

bool HandshakeConnectedState::transitionToIdle() {
    return transitionToIdleState;
}
