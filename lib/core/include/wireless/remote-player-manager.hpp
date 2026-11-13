#pragma once

#include <vector>
#include <cstring>  // For memcpy
#include <string>

#include "device/device-type.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "game/player.hpp"
#include "id-generator.hpp"
#include "utils/simple-timer.hpp"

// The kPlayerInfoBroadcast beacon. senderDeviceType is a DeviceType: a receiver
// that only wants FDNs has nothing else in the frame to tell one by.
struct PlayerInfoPkt {
    char id[IdGenerator::UUID_BUFFER_SIZE];  // UUID string + null terminator
    Allegiance allegiance;
    uint8_t hunter;
    uint8_t senderDeviceType;
} __attribute__((packed));

// How long a beacon the radio refused waits before it is offered again.
constexpr unsigned long BEACON_RETRY_AFTER_REFUSAL_MS = 250;

class RemotePlayerManager
{
public:
    /** @p deviceType is what this device's beacon says its sender is. */
    RemotePlayerManager(PeerCommsInterface* peerComms, DeviceType deviceType);

    /**
     * Sends the beacon when it is due; one the radio refuses is offered again
     * after BEACON_RETRY_AFTER_REFUSAL_MS. Call every tick.
     */
    void update();

    /** Starts the beacon; the first one is due at once. */
    void startBroadcastingPlayerInfo(Player* playerInfo, unsigned long broadcastIntervalMillis);

    int ProcessPlayerInfoPkt(const uint8_t* srcMacAddr, const uint8_t* data, const size_t dataLen);

    int getLastRssi() const;
    bool consumePacketReceived();

protected:
    int BroadcastPlayerInfo();
    PeerCommsInterface* peerComms;
    DeviceType deviceType;
    Player* localPlayerInfo = nullptr;
    unsigned long broadcastInterval;
    // Not running means a beacon is due; it runs for the interval after a
    // beacon the radio took and for the retry wait after one it refused.
    SimpleTimer broadcastTimer;
    // Same sentinel as the driver's: 0 would read as the strongest signal.
    int lastRssi = PeerCommsInterface::RSSI_UNKNOWN;
    bool packetReceived = false;

};
