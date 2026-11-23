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

using DeviceTestPacketCallback = std::function<void(
const uint8_t* fromMac, DeviceTestCommand command, const uint8_t* payload, size_t payloadLen)>;

using DeviceTestPeerFilter = std::function<bool(const uint8_t* mac)>;

class DeviceTestWirelessManager {
public:
    /** Constructs an uninitialized manager; call @ref initialize before use. */
    DeviceTestWirelessManager();
    /** Unregisters ESP-NOW handler if still registered. */
    ~DeviceTestWirelessManager();

    /** Stores @p wirelessManager and prepares ESP-NOW dispatch. */
    void initialize(WirelessManager* wirelessManager);
    /** Registers the ESP-NOW receive handler on the wireless manager. */
    void registerEspNowHandler();
    /** Removes the ESP-NOW receive handler. */
    void unregisterEspNowHandler();

    /** Parses one ESP-NOW payload; returns 1 on success, -1 on failure. */
    int processDeviceTestPacket(const uint8_t* macAddress, const uint8_t* data, size_t dataLen);

    /** Sets the callback invoked for each accepted device-test packet. */
    void setPacketReceivedCallback(DeviceTestPacketCallback callback);
    /** Restricts which peer MACs are accepted; null accepts all. */
    void setPeerFilter(DeviceTestPeerFilter filter);
    /** Clears packet and peer callbacks. */
    void clearCallbacks();

    /** Sends @p command with optional @p payload to @p dstMac. */
    int sendCommand(const uint8_t* dstMac, DeviceTestCommand command, const uint8_t* payload,
                    size_t payloadLen);

    /** Queues @ref DeviceTestCommand::BEGIN_SESSION to @p dstMac. */
    int sendBeginSession(const uint8_t* dstMac);
    /** Queues @ref DeviceTestCommand::END_SESSION to @p dstMac. */
    int sendEndSession(const uint8_t* dstMac);
    /** Queues @ref DeviceTestCommand::SHOW_SCREEN_PATTERN to @p dstMac. */
    int sendShowScreenPattern(const uint8_t* dstMac);
    /** Queues @ref DeviceTestCommand::SHOW_ALL_LEDS to @p dstMac. */
    int sendShowAllLeds(const uint8_t* dstMac);
    /** Queues @ref DeviceTestCommand::CLEAR_LEDS to @p dstMac. */
    int sendClearLeds(const uint8_t* dstMac);
    /** Queues @ref DeviceTestCommand::TRIGGER_HAPTICS to @p dstMac. */
    int sendTriggerHaptics(const uint8_t* dstMac);
    /** Queues @ref DeviceTestCommand::SESSION_INFO with @p commitHash8 to @p dstMac. */
    int sendSessionInfo(const uint8_t* dstMac, const char* commitHash8);

private:
    WirelessManager* wirelessManager = nullptr;
    bool espNowHandlerRegistered = false;
    DeviceTestPacketCallback packetReceivedCallback;
    DeviceTestPeerFilter peerFilter;
    uint8_t nextSeqId = 1;
};
