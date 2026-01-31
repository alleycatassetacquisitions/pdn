#include "wireless/remote-debug-manager.hpp"
#include "device/drivers/logger.hpp"


RemoteDebugManager::RemoteDebugManager(PeerCommsInterface* peerComms) {
    this->peerComms = peerComms;
}

RemoteDebugManager::~RemoteDebugManager() {
    peerComms = nullptr;
}

void RemoteDebugManager::Initialize(const std::string& ssid, const std::string& password, const std::string& baseUrl) {
    debugPacket.command = CHANGE_WIFI_CREDENTIALS;
    strncpy(debugPacket.ssid, ssid.c_str(), sizeof(debugPacket.ssid) - 1);
    strncpy(debugPacket.password, password.c_str(), sizeof(debugPacket.password) - 1);
    strncpy(debugPacket.baseUrl, baseUrl.c_str(), sizeof(debugPacket.baseUrl) - 1);
    debugPacket.ssid[sizeof(debugPacket.ssid) - 1] = '\0';
    debugPacket.password[sizeof(debugPacket.password) - 1] = '\0';
    debugPacket.baseUrl[sizeof(debugPacket.baseUrl) - 1] = '\0';
}

void RemoteDebugManager::SetPacketReceivedCallback(const std::function<void(DebugPacket)>& callback) {
    packetReceivedCallback = callback;
}

int RemoteDebugManager::ProcessDebugPacket(const uint8_t* srcMacAddr, const uint8_t* data, const size_t dataLen) {
    if (dataLen != sizeof(DebugPacket)) {
        return -1; // Invalid packet size
    }
    
    DebugPacket receivedPacket;
    memcpy(&receivedPacket, data, sizeof(DebugPacket));
    
    if (receivedPacket.command < 0 || receivedPacket.command >= DEBUG_COMMAND_COUNT) {
        return -2; // Invalid command
    }
    
    if (packetReceivedCallback) {
        packetReceivedCallback(receivedPacket);
    }
    
    return 0; // Success
}

int RemoteDebugManager::BroadcastDebugPacket() {
    return peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kDebugPacket, 
                                  (uint8_t*)&debugPacket, sizeof(DebugPacket));
}

void RemoteDebugManager::ClearCallbacks() {
    packetReceivedCallback = nullptr;
} 