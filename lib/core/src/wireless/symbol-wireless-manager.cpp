#include "wireless/symbol-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include <cstring>

static const char* const SWM_TAG = "SWM";

SymbolWirelessManager::SymbolWirelessManager(WirelessManager* wirelessManager,
                                             RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : wirelessManager(wirelessManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator)
    , resender(wirelessManager)
    // KEEP_DISTINCT: SEND_SYMBOL, SYMBOL_MATCH_SUCCESS and SYMBOLS_REFRESHED
    // share this PktType, so one must not cancel another's retries.
    , channel(wirelessManager, &resender, PktType::kSymbolMatchCommand, nullptr,
              Resender::SendMode::KEEP_DISTINCT) {
    std::memset(macPeer, 0, sizeof(macPeer));
    // No abandon callback: a symbol exchange that never lands is left to the
    // cable check in SymbolState, which leaves the state when the FDN goes away.
    channel.onReceive([this](const uint8_t* fromMac, const SymbolMatchPacket& packet) {
        onSymbolPacket(fromMac, packet);
    });
}

void SymbolWirelessManager::sync() {
    resender.sync();
}

void SymbolWirelessManager::setMacPeer(const uint8_t* macAddress) {
    memcpy(macPeer, macAddress, 6);
}

void SymbolWirelessManager::sendPacket(int command, SymbolId symbolId, SerialIdentifier serialPort) {
    SymbolMatchPacket packet{};
    packet.command = command;
    packet.symbolId = symbolId;

    LOG_W(SWM_TAG,
          "TX symbol command %d to %s (symbolId=%d, targetPort=%d)",
          command,
          MacToString(macPeer),
          static_cast<int>(symbolId),
          static_cast<int>(serialPort));

    channel.sendReliable(macPeer, packet);
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
    // board does not have carries no peers. A 2-node ring points both jacks at
    // the same peer, so the first match wins and the callback for that jack is
    // the one that runs.
    for (SerialIdentifier port : RemoteDeviceCoordinator::HELLO_JACKS) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac != nullptr && std::memcmp(peerMac, macAddress, 6) == 0) {
            resolvedPort = port;
            portResolved = true;
            break;
        }
    }

    if (!portResolved) {
        LOG_W(SWM_TAG, "No matching input port for symbol packet from %s", MacToString(macAddress));
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
