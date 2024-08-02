#pragma once

#include <vector>
#include <queue>
#include <UUID.h>
#include <esp_now.h>

#include "player.hpp"

struct RemotePlayer
{
    uint8_t wifiMacAddr[6];
    Player playerInfo;
    unsigned long lastSeenTime;
    signed rssi;

    RemotePlayer(const uint8_t* macAddr, String id, Allegiance allegiance, bool isHunter,
                 unsigned long lastSeen, signed rssiDb) :
                 playerInfo(id, allegiance, isHunter),
                 lastSeenTime(lastSeen),
                 rssi(rssiDb)
    {
        memcpy(wifiMacAddr, macAddr, 6);
    }
};

class EspNowManager
{
public:
    static EspNowManager* GetInstance();

    void StartBroadcastingPlayerInfo(Player* playerInfo, unsigned long broadcastIntervalMillis);
    
    void SetRemotePlayerTTL(unsigned long ttl);
    unsigned long GetRemotePlayerTTL();

    void Update();
private:
    EspNowManager();

    int BroadcastPlayerInfo();

    //Queues up data for sending, may not send right away
    int SendData(const uint8_t* dstMac, const uint8_t* data, size_t len);

    //TODO: Hooking this up should allow us to parse rssi from received WiFi pkts
    //static void WifiPromiscuousRecvCallback(void *buf, wifi_promiscuous_pkt_type_t type);
    static void EspNowRecvCallback(const uint8_t *mac_addr, const uint8_t *data, int data_len);
    static void EspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status);

    Player* m_localPlayerInfo;
    std::vector<RemotePlayer> m_remotePlayers;
    unsigned long m_remotePlayerTTL;
    unsigned long m_broadcastInterval;
    unsigned long m_lastBroadcastTime;

    struct DataSendBuffer
    {
        uint8_t dstMac[6];
        uint8_t* ptr;
        size_t len;
    };

    std::queue<DataSendBuffer> m_sendQueue;
};
