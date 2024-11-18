#include <Arduino.h>
#include "wireless/remote-player-manager.hpp"
#include "wireless/esp-now-comms.hpp"

#define DEBUG_REMOTE_PLAYER_MANAGER 0

struct PlayerInfoPkt
{
    char id[32];
    Allegiance allegiance;
    uint8_t hunter;
} __attribute__((packed));


RemotePlayerManager::RemotePlayerManager() :
    m_remotePlayerTTL(5000),
    m_broadcastInterval(1000),
    m_lastBroadcastTime(0)
{
}

void RemotePlayerManager::Update()
{
    unsigned long now = millis();

    //Remove players not seen in a long time
    //Single pass removal using Erase-move idiom
    m_remotePlayers.erase(
        std::remove_if(m_remotePlayers.begin(), m_remotePlayers.end(),
            [&now,this](RemotePlayer p){ return now - p.lastSeenTime > m_remotePlayerTTL; }),
        m_remotePlayers.end());

    //Broadcast if we need to
    if( (m_lastBroadcastTime != 0) && (now - m_lastBroadcastTime >= m_broadcastInterval))
    {
        BroadcastPlayerInfo();
    }
}

int RemotePlayerManager::BroadcastPlayerInfo()
{
    PlayerInfoPkt broadcastPkt;
    strncpy(broadcastPkt.id, m_localPlayerInfo->getUserID().c_str(), 32);
    broadcastPkt.id[31] = '\0'; //TODO: Should be 33 bytes since ID could be 32 chars
    broadcastPkt.allegiance = m_localPlayerInfo->getAllegiance();
    broadcastPkt.hunter = m_localPlayerInfo->isHunter();

#if DEBUG_REMOTE_PLAYER_MANAGER
    Serial.printf("Broadcasting player info. ID: %s Allegiance: %u %s (pktsize: %lu)\n",
                  broadcastPkt.id,
                  broadcastPkt.allegiance,
                  broadcastPkt.hunter ? "Hunter" : "Not Hunter",
                  sizeof(broadcastPkt));
#endif

    int ret = EspNowManager::GetInstance()->SendData(ESP_NOW_BROADCAST_ADDR,
                                                     PktType::kPlayerInfoBroadcast,
                                                     (uint8_t*)&broadcastPkt,
                                                     sizeof(broadcastPkt));
    m_lastBroadcastTime = millis();
    return ret;
}

void RemotePlayerManager::StartBroadcastingPlayerInfo(Player *playerInfo, unsigned long broadcastIntervalMillis)
{
    m_localPlayerInfo = playerInfo;
    m_broadcastInterval = broadcastIntervalMillis;
    BroadcastPlayerInfo();
}

void RemotePlayerManager::SetRemotePlayerTTL(unsigned long ttl)
{
    m_remotePlayerTTL = ttl;
}

unsigned long RemotePlayerManager::GetRemotePlayerTTL()
{
    return m_remotePlayerTTL;
}

int RemotePlayerManager::ProcessPlayerInfoPkt(const uint8_t* srcMacAddr, const uint8_t *data, const size_t dataLen)
{
    if(dataLen != sizeof(PlayerInfoPkt))
    {
        Serial.printf("Unexpected packet len for PlayerInfoPkt. Got %lu but expected %lu\n",
                      dataLen,
                      sizeof(PlayerInfoPkt));
        //TODO: Return correct error code
        return -1;
    }

    const PlayerInfoPkt* pkt = (const PlayerInfoPkt*)data;

    //Find our remote player record in local list
    auto remotePlayer = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(),
        [srcMacAddr](RemotePlayer rp) { return memcmp(srcMacAddr, rp.wifiMacAddr, ESP_NOW_ETH_ALEN) == 0;});

    //If we don't currently have a record for them, add them
    if(remotePlayer == m_remotePlayers.end())
    {

#if DEBUG_REMOTE_PLAYER_MANAGER
        Serial.printf("Discovered player %s Allegiance: %u IsHunter: %u\n", pkt->id, pkt->allegiance, pkt->hunter);
#endif

        m_remotePlayers.emplace_back(srcMacAddr, pkt->id, pkt->allegiance, pkt->hunter,
            millis(), 0);
        remotePlayer = m_remotePlayers.end() - 1;
        Serial.printf("Added discovered player %s (Allegiance: %u, %s) at addr %X:%X:%X:%X:%X:%X\n", 
            remotePlayer->playerInfo.getUserID().c_str(),
            remotePlayer->playerInfo.getAllegiance(),
            remotePlayer->playerInfo.isHunter() ? "Hunter" : "Not Hunter",
            srcMacAddr[0], srcMacAddr[1], srcMacAddr[2],
            srcMacAddr[3], srcMacAddr[4], srcMacAddr[5]);
    }

    //If we have a local record of this player, update their last seen time, regardless of packet type
    if(remotePlayer != m_remotePlayers.end())
    {
        remotePlayer->lastSeenTime = millis();
#if PDN_ENABLE_RSSI_TRACKING
        remotePlayer->rssi = EspNowManager::GetInstance()->GetRssiForPeer(srcMacAddr);
        //Serial.printf("Updated peer rssi to %i\n", remotePlayer->rssi);
#endif
    }
    return 0;
}
