#include <vector>
#include <esp_now.h>

#include "player.hpp"

struct RemotePlayer
{
    uint8_t wifiMacAddr[ESP_NOW_ETH_ALEN];
    Player playerInfo;
    unsigned long lastSeenTime;
    signed rssi;

    RemotePlayer(const uint8_t* macAddr, String id, Allegiance allegiance, bool isHunter,
                 unsigned long lastSeen, signed rssiDb) :
                 playerInfo(id, allegiance, isHunter),
                 lastSeenTime(lastSeen),
                 rssi(rssiDb)
    {
        memcpy(wifiMacAddr, macAddr, ESP_NOW_ETH_ALEN);
    }
};

class RemotePlayerManager
{
public:
    RemotePlayerManager();

    void Update();

    void StartBroadcastingPlayerInfo(Player* playerInfo, unsigned long broadcastIntervalMillis);
    
    void SetRemotePlayerTTL(unsigned long ttl);
    unsigned long GetRemotePlayerTTL();

    int ProcessPlayerInfoPkt(const uint8_t* srcMacAddr, const uint8_t* data, const size_t dataLen);

protected:
    int BroadcastPlayerInfo();

    Player* m_localPlayerInfo;
    std::vector<RemotePlayer> m_remotePlayers;
    unsigned long m_remotePlayerTTL;
    unsigned long m_broadcastInterval;
    unsigned long m_lastBroadcastTime;

};
