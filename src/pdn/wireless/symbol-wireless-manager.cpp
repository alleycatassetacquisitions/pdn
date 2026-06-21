#include "wireless/symbol-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include <cstring>

static const char* SWM_TAG = "SWM";

namespace {

bool macListedOnPort(RemoteDeviceCoordinator* rdc, SerialIdentifier port, const uint8_t* macAddress) {
    if (rdc == nullptr || macAddress == nullptr) {
        return false;
    }
    const uint8_t* directPeerMac = rdc->getPeerMac(port);
    if (directPeerMac != nullptr && std::memcmp(directPeerMac, macAddress, 6) == 0) {
        return true;
    }
    PortState portState = rdc->getPortState(port);
    for (const auto& peerMac : portState.peerMacAddresses) {
        if (std::memcmp(peerMac.data(), macAddress, 6) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

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
    packet.targetPort = static_cast<int>(serialPort);

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
          "RX symbol command %d from %s (symbolId=%d, targetPort=%d)",
          packet->command,
          MacToString(macAddress),
          static_cast<int>(packet->symbolId),
          packet->targetPort);

    if (remoteDeviceCoordinator == nullptr) {
        LOG_E(SWM_TAG, "RemoteDeviceCoordinator unavailable, dropping symbol packet");
        return -1;
    }

    SerialIdentifier resolvedPort = SerialIdentifier::OUTPUT_JACK;
    bool portResolved = false;

    const auto packetPort = static_cast<SerialIdentifier>(packet->targetPort);
    if (packetPort == SerialIdentifier::OUTPUT_JACK || packetPort == SerialIdentifier::INPUT_JACK) {
        if (macListedOnPort(remoteDeviceCoordinator, packetPort, macAddress)) {
            resolvedPort = packetPort;
            portResolved = true;
        }
    }

    if (!portResolved) {
        for (SerialIdentifier port : {SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK}) {
            if (macListedOnPort(remoteDeviceCoordinator, port, macAddress)) {
                resolvedPort = port;
                portResolved = true;
                break;
            }
        }
    }

    if (!portResolved) {
        LOG_W(SWM_TAG, "No matching input port for symbol packet from %s (targetPort=%d)",
              macAddress ? MacToString(macAddress) : "(null)",
              packet->targetPort);
        return -1;
    }

    SymbolMatchCommand command(macAddress, packet->command, packet->symbolId, resolvedPort);

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