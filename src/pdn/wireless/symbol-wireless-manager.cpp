#include "wireless/symbol-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include <cstring>

static const char* SWM_TAG = "SWM";

SymbolWirelessManager::SymbolWirelessManager() {
    std::memset(macPeer, 0, sizeof(macPeer));
    remoteDeviceCoordinator = nullptr;
}

SymbolWirelessManager::~SymbolWirelessManager() {
    wirelessManager = nullptr;
}

void SymbolWirelessManager::initialize(WirelessManager* wirelessManager, RemoteDeviceCoordinator* remoteDeviceCoordinator) {
    this->wirelessManager = wirelessManager;
    this->remoteDeviceCoordinator = remoteDeviceCoordinator;
}

void SymbolWirelessManager::setMacPeer(const uint8_t* macAddress) {
    memcpy(macPeer, macAddress, 6);
}

int SymbolWirelessManager::sendPacket(int command, SymbolId symbolId, SerialIdentifier serialPort) {
    SymbolMatchPacket packet{};
    packet.command = command;
    packet.symbolId = symbolId;

    LOG_W(SWM_TAG,
          "TX symbol command %d to %s (symbolId=%d, targetPort=%d)",
          command,
          MacToString(macPeer),
          static_cast<int>(symbolId),
          static_cast<int>(serialPort));

    return wirelessManager->sendEspNowData(macPeer, PktType::kSymbolMatchCommand, (uint8_t*)&packet, sizeof(packet));
}

int SymbolWirelessManager::processSymbolMatchCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen) {

    if (dataLen != sizeof(SymbolMatchPacket)) {
        LOG_E(SWM_TAG, "RX symbol pkt bad len: got %u expected %u from %s",
              static_cast<unsigned>(dataLen),
              static_cast<unsigned>(sizeof(SymbolMatchPacket)),
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    const SymbolMatchPacket* packet = reinterpret_cast<const SymbolMatchPacket*>(data);

    LOG_W(SWM_TAG,
          "RX symbol command %d from %s (symbolId=%d)",
          packet->command,
          MacToString(macAddress),
          static_cast<int>(packet->symbolId));

    if (remoteDeviceCoordinator == nullptr) {
        LOG_E(SWM_TAG, "RemoteDeviceCoordinator unavailable, dropping symbol packet");
        return -1;
    }

    SerialIdentifier resolvedPort = SerialIdentifier::OUTPUT_JACK;
    bool portResolved = false;

    for (SerialIdentifier port : {SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK}) {
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
        return -1;
    }

    SymbolMatchCommand command(macAddress, packet->command, packet->symbolId);

    auto callbackIt = packetReceivedCallbacks.find(resolvedPort);
    if (callbackIt != packetReceivedCallbacks.end() && callbackIt->second) {
        callbackIt->second(command);
    }

    return 1;
}

void SymbolWirelessManager::setPacketReceivedCallback(const std::function<void(const SymbolMatchCommand&)>& callback, SerialIdentifier port) {
    packetReceivedCallbacks[port] = callback;
}

void SymbolWirelessManager::clearCallback() {
    packetReceivedCallbacks.clear();
}