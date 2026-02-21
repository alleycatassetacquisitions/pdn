#include "apps/wireless-app-launcher.hpp"

struct AppCommandPacket {
    uint8_t macAddress[6];
    uint8_t command;
    uint8_t appId;
} __attribute__((packed));

WirelessAppLauncher::WirelessAppLauncher() {
    wirelessManager = nullptr;
    packetReceivedCallback = nullptr;
}

WirelessAppLauncher::~WirelessAppLauncher() {
    wirelessManager = nullptr;
    packetReceivedCallback = nullptr;
}

void WirelessAppLauncher::initialize(WirelessManager* wirelessManager) {
    this->wirelessManager = wirelessManager;
}

void WirelessAppLauncher::setPacketReceivedCallback(const std::function<void(const WirelessAppCommand&)>& callback) {
    packetReceivedCallback = callback;
}

int WirelessAppLauncher::sendAppCommand(const uint8_t* macAddress, AppCommand command, AppId appId) {
    WirelessAppCommand appCommand(macAddress, command, appId);
    return wirelessManager->sendEspNowData(
        macAddress, 
        PktType::kWirelessAppCommand, 
        reinterpret_cast<const uint8_t*>(&appCommand), 
        sizeof(appCommand));
}

void WirelessAppLauncher::clearCallbacks() {
    packetReceivedCallback = nullptr;
}

int WirelessAppLauncher::processAppCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen) {
    if(dataLen != sizeof(AppCommandPacket)) {
        return -1;
    }
    
    const AppCommandPacket* packet = reinterpret_cast<const AppCommandPacket*>(data);
    
    if(packetReceivedCallback) {
        WirelessAppCommand command(macAddress, static_cast<AppCommand>(packet->command), static_cast<AppId>(packet->appId));
        packetReceivedCallback(command);
    }
    return 1;
}
