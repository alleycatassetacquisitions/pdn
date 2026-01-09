#include "wireless/remote-debug-manager.hpp"
#include "wireless/esp-now-comms.hpp"
#include <Arduino.h>
#include <esp_log.h>

RemoteDebugManager* RemoteDebugManager::s_instance = nullptr;

RemoteDebugManager* RemoteDebugManager::GetInstance() {
    if (s_instance == nullptr) {
        s_instance = new RemoteDebugManager();
    }
    return s_instance;
}

RemoteDebugManager::RemoteDebugManager() {
}

void RemoteDebugManager::Initialize(std::string ssid, std::string password, std::string baseUrl) {
    m_debugPacket.command = CHANGE_WIFI_CREDENTIALS;
    strncpy(m_debugPacket.ssid, ssid.c_str(), sizeof(m_debugPacket.ssid) - 1);
    strncpy(m_debugPacket.password, password.c_str(), sizeof(m_debugPacket.password) - 1);
    strncpy(m_debugPacket.baseUrl, baseUrl.c_str(), sizeof(m_debugPacket.baseUrl) - 1);
    m_debugPacket.ssid[sizeof(m_debugPacket.ssid) - 1] = '\0';
    m_debugPacket.password[sizeof(m_debugPacket.password) - 1] = '\0';
    m_debugPacket.baseUrl[sizeof(m_debugPacket.baseUrl) - 1] = '\0';
}

void RemoteDebugManager::SetPacketReceivedCallback(std::function<void(DebugPacket)> callback) {
    m_packetReceivedCallback = callback;
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
    
    if (m_packetReceivedCallback) {
        m_packetReceivedCallback(receivedPacket);
    }
    
    return 0; // Success
}

int RemoteDebugManager::BroadcastDebugPacket() {
    
    // Get the ESP-NOW manager instance
    ESP_LOGI("RemoteDebugManager", "Getting ESP-NOW manager instance for broadcasting debug packet");
    EspNowManager* espNowManager = EspNowManager::GetInstance();
    ESP_LOGI("RemoteDebugManager", "Broadcasting debug packet with command: %d", m_debugPacket.command);
    
    // Broadcast the packet using ESP-NOW
    return espNowManager->sendData(PEER_BROADCAST_ADDR, static_cast<uint8_t>(PktType::kDebugPacket), 
                                  (uint8_t*)&m_debugPacket, sizeof(DebugPacket));
}

void RemoteDebugManager::ClearCallbacks() {
    m_packetReceivedCallback = nullptr;
} 