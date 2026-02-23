//
// Created by Elli Furedy on 2/22/2025.
//
#include <cstring>
#include "wireless/handshake-wireless-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"

struct HandshakePacket {
    int jack;
    int command;
} __attribute__((packed));

// Packets always travel OUTPUT<->INPUT, so the receiving port is always
// the opposite of the sending port.
static SerialIdentifier invertJack(SerialIdentifier jack) {
    return jack == SerialIdentifier::OUTPUT_JACK ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
}

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

void HandshakeWirelessManager::setMacPeer(const uint8_t* macBytes, SerialIdentifier jack) {
    memcpy(macPeers[jack].data(), macBytes, 6);
}

void HandshakeWirelessManager::removeMacPeer(SerialIdentifier jack) {
    macPeers.erase(jack);
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
    hsPacket.jack = static_cast<int>(jack);

    LOG_I("HWM", "Sending command %i to port %i", command, static_cast<int>(jack));

    return wirelessManager->sendEspNowData(
        it->second.data(),
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

    // The connection is always OUTPUT<->INPUT, so the local port that should
    // handle this packet is always the opposite of the sender's port.
    SerialIdentifier routeTo = invertJack(static_cast<SerialIdentifier>(packet->jack));

    HandshakeCommand command(macAddress, packet->command, routeTo);

    auto it = callbacks.find(routeTo);
    if (it != callbacks.end() && it->second) {
        it->second(command);
    }

    return 1;
}
