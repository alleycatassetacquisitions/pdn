#pragma once

#include <vector>
#include <cstring>  // For memcpy
#include <string>

#include "device/drivers/peer-comms-interface.hpp"
#include "game/player.hpp"

struct RemotePlayer
{
    uint8_t wifiMacAddr[6];
    Player playerInfo;
    unsigned long lastSeenTime;
    signed rssi;

    RemotePlayer(const uint8_t* macAddr, std::string id, Allegiance allegiance, bool isHunter,
                 unsigned long lastSeen, signed rssiDb) :
                 playerInfo(id, allegiance, isHunter),
                 lastSeenTime(lastSeen),
                 rssi(rssiDb)
    {
        memcpy(wifiMacAddr, macAddr, 6);
    }
};

class RemotePlayerManager
{
public:
    RemotePlayerManager(PeerCommsInterface* peerComms);

    void Update();

    void StartBroadcastingPlayerInfo(Player* playerInfo, unsigned long broadcastIntervalMillis);
    
    void SetRemotePlayerTTL(unsigned long ttl);
    unsigned long GetRemotePlayerTTL();

    int ProcessPlayerInfoPkt(const uint8_t* srcMacAddr, const uint8_t* data, const size_t dataLen);

protected:
    int BroadcastPlayerInfo();
    PeerCommsInterface* peerComms;
    Player* localPlayerInfo;
    std::vector<RemotePlayer> remotePlayers;
    unsigned long remotePlayerTTL;
    unsigned long broadcastInterval;
    unsigned long lastBroadcastTime;

};
