#pragma once

#include <cstdint>
#include <cstring>

#include "apps/app-registry.hpp"
#include "device/wireless-manager.hpp"

enum AppCommand {
    APP_LAUNCH = 0,
    APP_LAUNCH_ACK = 1,
};

struct WirelessAppCommand {
    uint8_t macAddress[6];
    AppCommand command;
    AppId appId;

    WirelessAppCommand(const uint8_t* macAddress, AppCommand command, AppId appId) : command(command), appId(appId) {
        if(macAddress) {
            memcpy(this->macAddress, macAddress, 6);
        } else {
            memset(this->macAddress, 0, 6);
        }
    }
};

class WirelessAppLauncher {
public:
    WirelessAppLauncher();
    ~WirelessAppLauncher();

    void initialize(WirelessManager* wirelessManager);

    int processAppCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    void setPacketReceivedCallback(const std::function<void(const WirelessAppCommand&)>& callback);

    int sendAppCommand(const uint8_t* macAddress, AppCommand command, AppId appId);

    void clearCallbacks();

private:
    WirelessManager* wirelessManager;

    std::function<void(const WirelessAppCommand&)> packetReceivedCallback;
};