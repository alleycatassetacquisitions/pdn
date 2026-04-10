#include "wireless/symbol-wireless-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include <cstring>

static const char* SWM_TAG = "SWM";

SymbolWirelessManager::SymbolWirelessManager() {
    std::memset(macPeer, 0, sizeof(macPeer));
}

SymbolWirelessManager::~SymbolWirelessManager() {
    wirelessManager = nullptr;
}

void SymbolWirelessManager::initialize(WirelessManager* wirelessManager) {
    this->wirelessManager = wirelessManager;
}

void SymbolWirelessManager::setMacPeer(const uint8_t* macAddress) {
    memcpy(macPeer, macAddress, 6);
}

int SymbolWirelessManager::sendPacket(int command, SymbolId symbolId, SerialIdentifier serialPort) {
    SymbolMatchPacket packet{};
    packet.command = command;
    packet.symbolId = symbolId;
    packet.serialPort = static_cast<int>(serialPort);

    LOG_W(SWM_TAG,
          "TX symbol command %d to %s (symbolId=%d, serialPort=%d)",
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
          "RX symbol command %d from %s (symbolId=%d, serialPort=%d)",
          packet->command,
          MacToString(macAddress),
          static_cast<int>(packet->symbolId),
          packet->serialPort);

    SerialIdentifier port = static_cast<SerialIdentifier>(packet->serialPort);
    SymbolMatchCommand command(macAddress, packet->command, packet->symbolId, port);

    if (packetReceivedCallback) {
        packetReceivedCallback(command);
    }

    return 1;
}

void SymbolWirelessManager::setPacketReceivedCallback(const std::function<void(const SymbolMatchCommand&)>& callback) {
    packetReceivedCallback = callback;
}

void SymbolWirelessManager::clearCallback() {
    packetReceivedCallback = nullptr;
}