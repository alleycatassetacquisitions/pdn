#include "wireless/symbol-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include <cstring>

static const char* const SWM_TAG = "SWM";

SymbolWirelessManager::SymbolWirelessManager(WirelessManager* wirelessManager,
                                             RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : remoteDeviceCoordinator(remoteDeviceCoordinator)
    , resender(wirelessManager)
    , channel(wirelessManager, PktType::kSymbolMatchCommand) {
    std::memset(macPeer, 0, sizeof(macPeer));
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

void SymbolWirelessManager::sendPacket(int command, SymbolId symbolId, SerialIdentifier jack) {
    SymbolMatchPacket packet{};
    packet.command = command;
    packet.symbolId = symbolId;

    LOG_W(SWM_TAG, "TX symbol command %d to %s (symbolId=%d, targetJack=%d)", command,
          MacToString(macPeer), static_cast<int>(symbolId), static_cast<int>(jack));

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

    for (SerialIdentifier jack : RemoteDeviceCoordinator::HELLO_JACKS) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(jack);
        if (peerMac != nullptr && std::memcmp(peerMac, macAddress, 6) == 0) {
            auto callbackIt = packetReceivedCallbacks.find(jack);
            if (callbackIt != packetReceivedCallbacks.end() && callbackIt->second) {
                if (packet.command == SMCommand::SYMBOLS_REFRESHED ||
                    packet.command == SMCommand::SYMBOL_MATCH_SUCCESS) {
                    channel.cancel(macAddress);
                }
                callbackIt->second(SymbolMatchCommand{packet.command, packet.symbolId});
            }
            return;
        }
    }

    LOG_W(SWM_TAG, "No matching input jack for symbol packet from %s", MacToString(macAddress));
}

void SymbolWirelessManager::setPacketReceivedCallback(
const std::function<void(const SymbolMatchCommand&)>& callback, SerialIdentifier jack) {
    packetReceivedCallbacks[jack] = callback;
}

void SymbolWirelessManager::clearCallback() {
    packetReceivedCallbacks.clear();
    channel.cancelAll();
}
