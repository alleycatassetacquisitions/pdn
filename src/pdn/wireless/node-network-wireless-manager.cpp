#include "wireless/node-network-wireless-manager.hpp"

#include <cstring>

#define TAG "EDWM"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"

void NodeNetworkWirelessManager::initialize(WirelessManager* wirelessManager,
                                            PlatformClock* platformClock) {
    this->wirelessManager = wirelessManager;
    this->platformClock = platformClock;
}

void NodeNetworkWirelessManager::setClock(PlatformClock* platformClock) {
    this->platformClock = platformClock;
}

void NodeNetworkWirelessManager::setPacketReceivedCallback(const PacketCallback& callback) {
    packetReceivedCallback = callback;
}

void NodeNetworkWirelessManager::setDeliveryCallback(const DeliveryCallback& callback) {
    deliveryCallback = callback;
}

void NodeNetworkWirelessManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
    deliveryCallback = nullptr;
}

unsigned long NodeNetworkWirelessManager::nowMs() const {
    return platformClock != nullptr ? platformClock->milliseconds() : 0;
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

    const uint8_t command = static_cast<uint8_t>(packet.command);
    if (command < static_cast<uint8_t>(NodeNetworkCommand::OFFER) ||
        command > static_cast<uint8_t>(NodeNetworkCommand::INPUT_PRESS)) {
        LOG_E(TAG, "Dropped event duel frame: command %u", command);
        return -1;
    }

    if (packet.command == static_cast<uint8_t>(NodeNetworkCommand::ACK)) {
        onAck(senderMac, packet);
        return 1;
    }

    if (packet.reliable == 1) {
        sendAck(senderMac, packet);
        if (rememberReliable(senderMac, packet)) {
            return 1;
        }
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

int NodeNetworkWirelessManager::sendReliable(const uint8_t* destinationMac,
                                             const NodeNetworkPacket& packet) {
    for (PendingPacket& slot : pending) {
        if (slot.active) {
            continue;
        }
        slot.active = true;
        memcpy(slot.destinationMac, destinationMac, sizeof(slot.destinationMac));
        slot.packet = packet;
        slot.packet.reliable = 1;
        slot.packet.sequenceNumber = nextSequenceNumber++;
        slot.attempts = 1;
        slot.lastSentMs = nowMs();
        return sendPacket(slot.destinationMac, slot.packet);
    }
    LOG_E(TAG, "reliable send of cmd %u dropped: pending table full", packet.command);
    return -1;
}

void NodeNetworkWirelessManager::update() {
    const unsigned long now = nowMs();
    for (PendingPacket& slot : pending) {
        if (!slot.active || now - slot.lastSentMs < NODE_RELIABLE_RETRY_MS) {
            continue;
        }
        if (slot.attempts >= NODE_RELIABLE_MAX_ATTEMPTS) {
            slot.active = false;
            LOG_W(TAG, "cmd %u seq %u to %s undelivered after %u sends", slot.packet.command,
                  slot.packet.sequenceNumber, MacToString(slot.destinationMac), slot.attempts);
            if (deliveryCallback) {
                deliveryCallback(slot.destinationMac, slot.packet, false);
            }
            continue;
        }
        slot.attempts++;
        slot.lastSentMs = now;
        sendPacket(slot.destinationMac, slot.packet);
    }
}

size_t NodeNetworkWirelessManager::getPendingReliableCount() const {
    size_t count = 0;
    for (const PendingPacket& slot : pending) {
        if (slot.active) {
            count++;
        }
    }
    return count;
}

void NodeNetworkWirelessManager::onAck(const uint8_t* senderMac, const NodeNetworkPacket& ack) {
    for (PendingPacket& slot : pending) {
        if (!slot.active || slot.packet.sequenceNumber != ack.sequenceNumber ||
            memcmp(slot.destinationMac, senderMac, sizeof(slot.destinationMac)) != 0) {
            continue;
        }
        slot.active = false;
        if (deliveryCallback) {
            deliveryCallback(slot.destinationMac, slot.packet, true);
        }
        return;
    }
}

void NodeNetworkWirelessManager::sendAck(const uint8_t* destinationMac,
                                         const NodeNetworkPacket& packet) {
    NodeNetworkPacket ack;
    ack.command = static_cast<uint8_t>(NodeNetworkCommand::ACK);
    ack.senderIsInitiator = packet.senderIsInitiator == 1 ? 0 : 1;
    ack.sequenceNumber = packet.sequenceNumber;
    memcpy(ack.matchId, packet.matchId, sizeof(ack.matchId));
    sendPacket(destinationMac, ack);
}

bool NodeNetworkWirelessManager::rememberReliable(const uint8_t* senderMac,
                                                  const NodeNetworkPacket& packet) {
    for (const SeenPacket& entry : seen) {
        if (entry.used && entry.sequenceNumber == packet.sequenceNumber &&
            memcmp(entry.senderMac, senderMac, sizeof(entry.senderMac)) == 0 &&
            memcmp(entry.matchId, packet.matchId, sizeof(entry.matchId)) == 0) {
            return true;
        }
    }
    SeenPacket& slot = seen[nextSeenSlot];
    nextSeenSlot = (nextSeenSlot + 1) % NODE_RELIABLE_SEEN_CAPACITY;
    slot.used = true;
    memcpy(slot.senderMac, senderMac, sizeof(slot.senderMac));
    memcpy(slot.matchId, packet.matchId, sizeof(slot.matchId));
    slot.sequenceNumber = packet.sequenceNumber;
    return false;
}
