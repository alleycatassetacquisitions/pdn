#include "apps/fdn-connect/fdn-connect-wireless-manager.hpp"
#include "device/drivers/logger.hpp"



FDNConnectWirelessManager::FDNConnectWirelessManager() {
}

FDNConnectWirelessManager::~FDNConnectWirelessManager() {

}

void FDNConnectWirelessManager::initialize(WirelessManager* wirelessManager) {
    this->wirelessManager = wirelessManager;
}

void FDNConnectWirelessManager::setPacketReceivedCallback(const std::function<void(const FDNConnectCommand&)>& callback) {
    this->packetReceivedCallback = callback;
}

void FDNConnectWirelessManager::clearCallbacks() {
    packetReceivedCallback = nullptr;
}

int FDNConnectWirelessManager::broadcastPacket(const uint8_t macAddress[6], FDNConnectCommand& command) {
    FDNConnectPacket fdnConnectPacket = FDNConnectPacket();
    fdnConnectPacket.command = command.command;
    return wirelessManager->sendEspNowData(macAddress, PktType::kFDNConnectCommand, reinterpret_cast<const uint8_t*>(&fdnConnectPacket), sizeof(fdnConnectPacket));
}

int FDNConnectWirelessManager::processFDNConnectCommand(const uint8_t* src, const uint8_t* data, const size_t len) {
    
    if(len != sizeof(FDNConnectPacket)) {
        LOG_E("FDNConnectWirelessManager", "Unexpected packet length for FDNConnectPacket. Got %lu but expected %lu", len, sizeof(FDNConnectPacket));
        return -1;
    }

    const FDNConnectPacket* fdnConnectPacket = reinterpret_cast<const FDNConnectPacket*>(data);

    FDNConnectCommand fdnConnectCommand(fdnConnectPacket->command);

    if(packetReceivedCallback) {
        packetReceivedCallback(fdnConnectCommand);
    }
    
    return 0;
}