#pragma once

#include <vector>
#include <string>
#include <cstring>  // For memcpy
#include <functional>
#include "device/drivers/peer-comms-interface.hpp"

enum DebugCommand {
    // Debug Commands
    CHANGE_WIFI_CREDENTIALS = 0,
    
    DEBUG_COMMAND_COUNT,  // Always add new commands above this line
    INVALID_DEBUG_COMMAND = 0xFF
};

struct DebugPacket {
    int command;
    char ssid[33];     // 32 characters + null terminator
    char password[33]; // 32 characters + null terminator
    char baseUrl[64];  // 63 characters + null terminator

    DebugPacket() : command(0) {
        memset(ssid, 0, sizeof(ssid));
        memset(password, 0, sizeof(password));
        memset(baseUrl, 0, sizeof(baseUrl));
    }

    DebugPacket(int cmd, const char* wifiSsid = "", const char* wifiPassword = "", const char* url = "") : command(cmd) {
        strncpy(ssid, wifiSsid, sizeof(ssid) - 1);
        strncpy(password, wifiPassword, sizeof(password) - 1);
        strncpy(baseUrl, url, sizeof(baseUrl) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        password[sizeof(password) - 1] = '\0';
        baseUrl[sizeof(baseUrl) - 1] = '\0';
    }
};

class RemoteDebugManager {
public:
    explicit RemoteDebugManager(PeerCommsInterface* peerComms);
    ~RemoteDebugManager();

    void Initialize(const std::string& ssid, const std::string& password, const std::string& baseUrl);
    
    void SetPacketReceivedCallback(const std::function<void(DebugPacket)>& callback);
    
    int ProcessDebugPacket(const uint8_t* srcMacAddr, const uint8_t* data, const size_t dataLen);
    
    int BroadcastDebugPacket();

    void ClearCallbacks();

private:
    PeerCommsInterface* peerComms;
    DebugPacket debugPacket;
    
    std::function<void(DebugPacket)> packetReceivedCallback;
    
    static RemoteDebugManager* instance;
};