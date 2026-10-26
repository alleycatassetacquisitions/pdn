#include "wireless/node-network-wireless-manager.hpp"

#include <cstring>

#define TAG "EDWM"
#include "device/drivers/logger.hpp"

void NodeNetworkWirelessManager::initialize(WirelessManager* wirelessManager) {
    this->wirelessManager = wirelessManager;
}

void NodeNetworkWirelessManager::setPacketReceivedCallback(const PacketCallback& callback) {
    packetReceivedCallback = callback;
}

void NodeNetworkWirelessManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
}

int NodeNetworkWirelessManager::processPacket(const uint8_t* senderMac, const uint8_t* data,
                                            size_t dataLength) {
    if (dataLength != sizeof(NodeNetworkPacket)) {
        LOG_E(TAG, "Dropped event duel frame: length %lu, expected %lu",
              static_cast<unsigned long>(dataLength),
              static_cast<unsigned long>(sizeof(NodeNetworkPacket)));
        return -1;
    }

    NodeNetworkPacket packet;
    memcpy(&packet, data, sizeof(packet));

    if (packet.protocolVersion != NODE_NETWORK_PROTOCOL_VERSION) {
        LOG_E(TAG, "Dropped event duel frame: version %u", packet.protocolVersion);
        return -1;
    }

    const uint8_t command = static_cast<uint8_t>(packet.command);
    if (command < static_cast<uint8_t>(NodeNetworkCommand::OFFER) ||
        command > static_cast<uint8_t>(NodeNetworkCommand::NEVER_PRESSED)) {
        LOG_E(TAG, "Dropped event duel frame: command %u", command);
        return -1;
    }

    if (packetReceivedCallback) {
        packetReceivedCallback(senderMac, packet);
    }
    return 1;
}

int NodeNetworkWirelessManager::sendPacket(const uint8_t* destinationMac,
                                         const NodeNetworkPacket& packet) {
    if (wirelessManager == nullptr) {
        return -1;
    }
    return wirelessManager->sendEspNowData(destinationMac, PktType::kNodeNetworkCommand,
                                           reinterpret_cast<const uint8_t*>(&packet),
                                           sizeof(packet));
}
