//
// Created by Elli Furedy on 2/22/2025.
//
#include <cstring>
#include "wireless/handshake-wireless-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"

struct HandshakePacket {
    int sendingJack;
    int receicingJack;
    int deviceType;
    int command;
} __attribute__((packed));

HandshakeWirelessManager::HandshakeWirelessManager() {}

HandshakeWirelessManager::~HandshakeWirelessManager() {
    wirelessManager = nullptr;
}

void HandshakeWirelessManager::initialize(WirelessManager* wirelessManager) {
    this->wirelessManager = wirelessManager;
}

void HandshakeWirelessManager::setPacketReceivedCallback(const std::function<void(HandshakeCommand)>& callback, SerialIdentifier jack) {
    callbacks[jack] = callback;
}

void HandshakeWirelessManager::clearCallback(SerialIdentifier jack) {
    callbacks.erase(jack);
}

void HandshakeWirelessManager::clearCallbacks() {
    callbacks.clear();
}

bool HandshakeWirelessManager::setMacPeer(SerialIdentifier jack, Peer peer) {
    // Reject self-MAC: a peer claiming to be us (via SEND_MAC_ADDRESS serial
    // or EXCHANGE_ID wireless) could be a self-loopback or a neighbor
    // spoofing our MAC. Never register as a direct peer — violates spec
    // invariant DirectPeerIsNeverSelf.
    if (wirelessManager != nullptr) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (selfMac != nullptr && memcmp(peer.macAddr.data(), selfMac, 6) == 0) {
            return false;
        }
    }
    macPeers[jack] = peer;
    return true;
}

void HandshakeWirelessManager::removeMacPeer(SerialIdentifier jack) {
    macPeers.erase(jack);
}

const Peer* HandshakeWirelessManager::getMacPeer(SerialIdentifier jack) const {
    auto it = macPeers.find(jack);
    if (it == macPeers.end()) return nullptr;
    return &it->second;
}

int HandshakeWirelessManager::sendPacket(int command, SerialIdentifier jack) {
    auto it = macPeers.find(jack);
    if (it == macPeers.end()) {
        LOG_E("HWM", "No peer registered for port %i, cannot send command %i",
              static_cast<int>(jack), command);
        return -1;
    }

    HandshakePacket hsPacket;
    hsPacket.command = command;
    hsPacket.sendingJack = static_cast<int>(jack);
    hsPacket.receicingJack = static_cast<int>(it->second.sid);

    LOG_I("HWM", "Sending command %i to port %i", command, static_cast<int>(jack));

    return wirelessManager->sendEspNowData(
        it->second.macAddr.data(),
        PktType::kHandshakeCommand,
        (uint8_t*)&hsPacket,
        sizeof(hsPacket));
}

int HandshakeWirelessManager::processHandshakeCommand(const uint8_t* macAddress, const uint8_t* data,
    const size_t dataLen) {

    if (dataLen != sizeof(HandshakePacket)) {
        LOG_E("HWM", "Unexpected packet len for HandshakePacket. Got %lu but expected %lu\n",
                      dataLen, sizeof(HandshakePacket));
        return -1;
    }

    HandshakePacket* packet = (HandshakePacket*)data;

    if (packet->command < 0 || packet->command >= HSCommand::HS_COMMAND_COUNT) {
        LOG_E("HWM", "Invalid command value %i in HandshakePacket, dropping", packet->command);
        return -1;
    }

    SerialIdentifier sendingJack = static_cast<SerialIdentifier>(packet->sendingJack);
    SerialIdentifier receivingJack = static_cast<SerialIdentifier>(packet->receicingJack);
    HandshakeCommand command(macAddress, packet->command, packet->deviceType, sendingJack, receivingJack);

    auto it = callbacks.find(receivingJack);
    if (it != callbacks.end() && it->second) {
        it->second(command);
    }

    return 1;
}
