#pragma once

#include "device/wireless-manager.hpp"
#include <functional>


struct FDNConnectPacket {
    int command;
} __attribute__((packed));

enum FDNCommand {
    CONNECT = 0,
    DISCONNECT = 1,
};

struct FDNConnectCommand {
    int command;

    FDNConnectCommand(int command) : command(command) {}
};




class FDNConnectWirelessManager {
public:
    FDNConnectWirelessManager();
    ~FDNConnectWirelessManager();

    void initialize(WirelessManager* wirelessManager);

    void setPacketReceivedCallback(const std::function<void(const FDNConnectCommand&)>& callback);
    int broadcastPacket(const uint8_t macAddress[6], FDNConnectCommand& command);
    void clearCallbacks();

    int processFDNConnectCommand(const uint8_t* src, const uint8_t* data, const size_t len);

private:
    WirelessManager* wirelessManager;
    std::function<void(const FDNConnectCommand&)> packetReceivedCallback;
};