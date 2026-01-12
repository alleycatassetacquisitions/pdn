#include <algorithm>
#include "logger.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/esp-now-comms.hpp"
#include "id-generator.hpp"
#include "utils/simple-timer.hpp"

#define DEBUG_REMOTE_PLAYER_MANAGER 0

struct PlayerInfoPkt
{
    char id[IdGenerator::UUID_BUFFER_SIZE];  // UUID string + null terminator
    Allegiance allegiance;
    uint8_t hunter;
} __attribute__((packed));


RemotePlayerManager::RemotePlayerManager() :
    m_remotePlayerTTL(60000),
    m_broadcastInterval(5000),
    m_lastBroadcastTime(0)
{
}

void RemotePlayerManager::Update()
{
    unsigned long now = SimpleTimer::getPlatformClock()->milliseconds();

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
    strncpy(broadcastPkt.id, m_localPlayerInfo->getUserID().c_str(), IdGenerator::UUID_STRING_LENGTH);
    broadcastPkt.id[IdGenerator::UUID_STRING_LENGTH] = '\0';  // Ensure null termination
    broadcastPkt.allegiance = m_localPlayerInfo->getAllegiance();
    broadcastPkt.hunter = m_localPlayerInfo->isHunter();

#if DEBUG_REMOTE_PLAYER_MANAGER
    LOG_D("RPM", "Broadcasting player info. ID: %s Allegiance: %u %s (pktsize: %lu)\n",
                  broadcastPkt.id,
                  broadcastPkt.allegiance,
                  broadcastPkt.hunter ? "Hunter" : "Not Hunter",
                  sizeof(broadcastPkt));
#endif

    int ret = EspNowManager::GetInstance()->sendData(PEER_BROADCAST_ADDR,
                                                     static_cast<uint8_t>(PktType::kPlayerInfoBroadcast),
                                                     (uint8_t*)&broadcastPkt,
                                                     sizeof(broadcastPkt));
    m_lastBroadcastTime = SimpleTimer::getPlatformClock()->milliseconds();
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
        LOG_E("RPM", "Unexpected packet len for PlayerInfoPkt. Got %lu but expected %lu\n",
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

        LOG_D("RPM", "Discovered player %s Allegiance: %u IsHunter: %u\n", pkt->id, pkt->allegiance, pkt->hunter);

        m_remotePlayers.emplace_back(srcMacAddr, pkt->id, pkt->allegiance, pkt->hunter,
            SimpleTimer::getPlatformClock()->milliseconds(), 0);
        remotePlayer = m_remotePlayers.end() - 1;
        LOG_I("RPM", "Added discovered player %s (Allegiance: %u, %s) at addr %X:%X:%X:%X:%X:%X\n", 
            remotePlayer->playerInfo.getUserID().c_str(),
            remotePlayer->playerInfo.getAllegiance(),
            remotePlayer->playerInfo.isHunter() ? "Hunter" : "Not Hunter",
            srcMacAddr[0], srcMacAddr[1], srcMacAddr[2],
            srcMacAddr[3], srcMacAddr[4], srcMacAddr[5]);
    }

    //If we have a local record of this player, update their last seen time, regardless of packet type
    if(remotePlayer != m_remotePlayers.end())
    {
        remotePlayer->lastSeenTime = SimpleTimer::getPlatformClock()->milliseconds();
#if PDN_ENABLE_RSSI_TRACKING
        remotePlayer->rssi = EspNowManager::GetInstance()->GetRssiForPeer(srcMacAddr);
        LOG_D("RPM", "Updated peer rssi to %i\n", remotePlayer->rssi);
#endif
    }
    return 0;
}
