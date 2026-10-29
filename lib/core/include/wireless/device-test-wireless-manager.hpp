#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "device/drivers/button.hpp"
#include "device/wireless-manager.hpp"

enum class DeviceTestCommand : uint8_t {
    BEGIN_SESSION = 0,
    SESSION_INFO = 1,
    SHOW_SCREEN_PATTERN = 2,
    SHOW_ALL_LEDS = 3,
    TRIGGER_HAPTICS = 4,
    BUTTON_EVENT = 5,
    END_SESSION = 6,
    CLEAR_LEDS = 7,
};

struct DeviceTestPacket {
    DeviceTestCommand command;
    uint8_t seqId;
    uint8_t payload[16];
} __attribute__((packed));

static_assert(sizeof(DeviceTestPacket) == 18, "DeviceTestPacket wire size");

using DeviceTestPacketCallback =
    std::function<void(const uint8_t* fromMac, DeviceTestCommand command, const uint8_t* payload, size_t payloadLen)>;

using DeviceTestPeerFilter = std::function<bool(const uint8_t* mac)>;

class DeviceTestWirelessManager {
public:
    DeviceTestWirelessManager();
    ~DeviceTestWirelessManager();

    void initialize(WirelessManager* wirelessManager);
    void registerEspNowHandler();
    void unregisterEspNowHandler();

    int processDeviceTestPacket(const uint8_t* macAddress, const uint8_t* data, size_t dataLen);

    void setPacketReceivedCallback(DeviceTestPacketCallback callback);
    void setPeerFilter(DeviceTestPeerFilter filter);
    void setSessionBeginCallback(std::function<void(const uint8_t* fromMac)> callback);
    void clearCallbacks();

    int sendCommand(const uint8_t* dstMac, DeviceTestCommand command, const uint8_t* payload, size_t payloadLen);

    int sendBeginSession(const uint8_t* dstMac);
    int sendEndSession(const uint8_t* dstMac);
    int sendShowScreenPattern(const uint8_t* dstMac);
    int sendShowAllLeds(const uint8_t* dstMac);
    int sendClearLeds(const uint8_t* dstMac);
    int sendTriggerHaptics(const uint8_t* dstMac);
    int sendSessionInfo(const uint8_t* dstMac, const char* commitHash8);

private:
    WirelessManager* wirelessManager = nullptr;
    bool espNowHandlerRegistered = false;
    DeviceTestPacketCallback packetReceivedCallback;
    DeviceTestPeerFilter peerFilter;
    std::function<void(const uint8_t* fromMac)> sessionBeginCallback;
    uint8_t nextSeqId = 1;
};
