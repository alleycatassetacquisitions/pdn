#include "wireless/symbol-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include <cstring>

static const char* const SWM_TAG = "SWM";

SymbolWirelessManager::SymbolWirelessManager()
    : wirelessManager(nullptr)
    , remoteDeviceCoordinator(nullptr) {
    std::memset(macPeer, 0, sizeof(macPeer));
}

SymbolWirelessManager::~SymbolWirelessManager() {
    delete transport;
    transport = nullptr;
    channel = nullptr;
    wirelessManager = nullptr;
}

void SymbolWirelessManager::initialize(WirelessManager* wirelessManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) {
    this->wirelessManager = wirelessManager;
    this->remoteDeviceCoordinator = remoteDeviceCoordinator;

    // Claiming the channel is what makes the receive path live; no separate
    // handler registration is owed. Abandonment is silent here because a symbol
    // exchange that never lands leaves the state's own buffer timer to end it.
    transport = new ReliableTransport(wirelessManager);
    channel = transport->channel<SymbolMatchPacket>(PktType::kSymbolMatchCommand);
    if (channel != nullptr) {
        channel->onReceive(
            [this](const uint8_t* fromMac, const SymbolMatchPacket& packet) {
                onSymbolPacket(fromMac, packet);
            });
    }
}

void SymbolWirelessManager::sync() {
    if (transport != nullptr) transport->sync();
}

void SymbolWirelessManager::setMacPeer(const uint8_t* macAddress) {
    memcpy(macPeer, macAddress, 6);
}

void SymbolWirelessManager::sendPacket(int command, SymbolId symbolId, SerialIdentifier serialPort) {
    if (channel == nullptr) {
        LOG_E(SWM_TAG, "No symbol channel, dropping command %d", command);
        return;
    }

    SymbolMatchPacket packet{};
    packet.command = command;
    packet.symbolId = symbolId;

    LOG_W(SWM_TAG,
          "TX symbol command %d to %s (symbolId=%d, targetPort=%d)",
          command,
          MacToString(macPeer),
          static_cast<int>(symbolId),
          static_cast<int>(serialPort));

    channel->sendReliable(macPeer, packet);
}

void SymbolWirelessManager::onSymbolPacket(const uint8_t* macAddress, const SymbolMatchPacket& packet) {
    LOG_W(SWM_TAG,
          "RX symbol command %d from %s (symbolId=%d)",
          packet.command,
          MacToString(macAddress),
          static_cast<int>(packet.symbolId));

    if (remoteDeviceCoordinator == nullptr) {
        LOG_E(SWM_TAG, "RemoteDeviceCoordinator unavailable, dropping symbol packet");
        return;
    }

    SerialIdentifier resolvedPort = SerialIdentifier::OUTPUT_JACK;
    bool portResolved = false;

    // Every jack, not the subset a given device type happens to use: a jack the
    // board does not have carries no peers, and the sender's MAC sits on exactly
    // one of them.
    for (SerialIdentifier port : RemoteDeviceCoordinator::HELLO_JACKS) {
        PortState portState = remoteDeviceCoordinator->getPortState(port);
        for (const auto& peerMac : portState.peerMacAddresses) {
            if (macAddress != nullptr && std::memcmp(peerMac.data(), macAddress, 6) == 0) {
                resolvedPort = port;
                portResolved = true;
                break;
            }
        }

        if (portResolved) {
            break;
        }
    }

    if (!portResolved) {
        LOG_W(SWM_TAG, "No matching input port for symbol packet from %s", macAddress ? MacToString(macAddress) : "(null)");
        return;
    }

    SymbolMatchCommand command(macAddress, packet.command, packet.symbolId);

    std::map<SerialIdentifier, std::function<void(const SymbolMatchCommand&)>>::iterator callbackIt =
        packetReceivedCallbacks.find(resolvedPort);
    if (callbackIt != packetReceivedCallbacks.end() && callbackIt->second) {
        callbackIt->second(command);
    }
}

void SymbolWirelessManager::setPacketReceivedCallback(const std::function<void(const SymbolMatchCommand&)>& callback, SerialIdentifier port) {
    packetReceivedCallbacks[port] = callback;
}

void SymbolWirelessManager::clearCallback() {
    packetReceivedCallbacks.clear();
}
