#pragma once

#include <vector>
#include <cstring>  // For memcpy
#include <string>

#include "device/device-type.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "game/player.hpp"
#include "id-generator.hpp"
#include "wireless/remote-player.hpp"

/// The kPlayerInfoBroadcast beacon; the struct layout is the wire format.
struct PlayerInfoPkt {
    char id[IdGenerator::UUID_BUFFER_SIZE];  // UUID string + null terminator
    Allegiance allegiance;
    uint8_t hunter;
    // A DeviceType; a PDN takes a beacon from an FDN as its cue to relay records.
    uint8_t senderDeviceType;
} __attribute__((packed));

class RemotePlayerManager
{
public:
    RemotePlayerManager(PeerCommsInterface* peerComms);

    void Update();

    /** Broadcasts now, then each interval from Update(); each beacon carries senderDeviceType. */
    void StartBroadcastingPlayerInfo(  // NOLINT(readability-identifier-naming)
    Player* playerInfo, unsigned long broadcastIntervalMillis, DeviceType senderDeviceType);

    void SetRemotePlayerTTL(unsigned long ttl);
    unsigned long GetRemotePlayerTTL();

    int ProcessPlayerInfoPkt(const uint8_t* srcMacAddr, const uint8_t* data, const size_t dataLen);

    int getLastRssi() const;
    int getRemotePlayerCount() const;
    bool consumePacketReceived();

protected:
    int BroadcastPlayerInfo();
    PeerCommsInterface* peerComms;
    Player* localPlayerInfo;
    DeviceType localDeviceType = DeviceType::UNKNOWN;
    std::vector<RemotePlayer> remotePlayers;
    unsigned long remotePlayerTTL;
    unsigned long broadcastInterval;
    unsigned long lastBroadcastTime;
    // Same sentinel as the driver's: 0 would read as the strongest signal.
    int lastRssi = PeerCommsInterface::RSSI_UNKNOWN;
    bool packetReceived = false;

};
