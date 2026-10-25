#pragma once

#include <vector>
#include <cstring>  // For memcpy
#include <string>

#include "device/drivers/peer-comms-interface.hpp"
#include "game/player.hpp"
#include "wireless/remote-player.hpp"

class RemotePlayerManager
{
public:
    RemotePlayerManager(PeerCommsInterface* peerComms);

    void Update();

    void StartBroadcastingPlayerInfo(Player* playerInfo, unsigned long broadcastIntervalMillis);
    
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
    std::vector<RemotePlayer> remotePlayers;
    unsigned long remotePlayerTTL;
    unsigned long broadcastInterval;
    unsigned long lastBroadcastTime;
    // Same sentinel as the driver's: 0 would read as the strongest signal.
    int lastRssi = PeerCommsInterface::RSSI_UNKNOWN;
    bool packetReceived = false;

};
