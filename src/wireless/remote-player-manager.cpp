#include <algorithm>
#include "device/drivers/logger.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "id-generator.hpp"
#include "utils/simple-timer.hpp"

#define DEBUG_REMOTE_PLAYER_MANAGER 0

struct PlayerInfoPkt
{
    char id[IdGenerator::UUID_BUFFER_SIZE];  // UUID string + null terminator
    Allegiance allegiance;
    uint8_t hunter;
} __attribute__((packed));


RemotePlayerManager::RemotePlayerManager(PeerCommsInterface* peerComms) :
    remotePlayerTTL(60000),
    broadcastInterval(5000),
    lastBroadcastTime(0)
{
    this->peerComms = peerComms;
}

void RemotePlayerManager::Update()
{
    unsigned long now = SimpleTimer::getPlatformClock()->milliseconds();

    //Remove players not seen in a long time
    //Single pass removal using Erase-move idiom
    remotePlayers.erase(
        std::remove_if(remotePlayers.begin(), remotePlayers.end(),
            [&now,this](RemotePlayer p){ return now - p.lastSeenTime > remotePlayerTTL; }),
        remotePlayers.end());

    //Broadcast if we need to
    if( (lastBroadcastTime != 0) && (now - lastBroadcastTime >= broadcastInterval))
    {
        BroadcastPlayerInfo();
    }
}

int RemotePlayerManager::BroadcastPlayerInfo()
{
    PlayerInfoPkt broadcastPkt;
    strncpy(broadcastPkt.id, localPlayerInfo->getUserID().c_str(), IdGenerator::UUID_STRING_LENGTH);
    broadcastPkt.id[IdGenerator::UUID_STRING_LENGTH] = '\0';  // Ensure null termination
    broadcastPkt.allegiance = localPlayerInfo->getAllegiance();
    broadcastPkt.hunter = localPlayerInfo->isHunter();

#if DEBUG_REMOTE_PLAYER_MANAGER
    LOG_D("RPM", "Broadcasting player info. ID: %s Allegiance: %u %s (pktsize: %lu)\n",
                  broadcastPkt.id,
                  broadcastPkt.allegiance,
                  broadcastPkt.hunter ? "Hunter" : "Not Hunter",
                  sizeof(broadcastPkt));
#endif

    int ret = peerComms->sendData(peerComms->getGlobalBroadcastAddress(),
                                                     PktType::kPlayerInfoBroadcast,
                                                     (uint8_t*)&broadcastPkt,
                                                     sizeof(broadcastPkt));
    lastBroadcastTime = SimpleTimer::getPlatformClock()->milliseconds();
    return ret;
}

void RemotePlayerManager::StartBroadcastingPlayerInfo(Player *playerInfo, unsigned long broadcastIntervalMillis)
{
    localPlayerInfo = playerInfo;
    broadcastInterval = broadcastIntervalMillis;
    BroadcastPlayerInfo();
}

void RemotePlayerManager::SetRemotePlayerTTL(unsigned long ttl)
{
    remotePlayerTTL = ttl;
}

unsigned long RemotePlayerManager::GetRemotePlayerTTL()
{
    return remotePlayerTTL;
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
    auto remotePlayer = std::find_if(remotePlayers.begin(), remotePlayers.end(),
        [srcMacAddr](RemotePlayer rp) { return memcmp(srcMacAddr, rp.wifiMacAddr, 6) == 0;});

    //If we don't currently have a record for them, add them
    if(remotePlayer == remotePlayers.end())
    {

        LOG_D("RPM", "Discovered player %s Allegiance: %u IsHunter: %u\n", pkt->id, pkt->allegiance, pkt->hunter);

        remotePlayers.emplace_back(srcMacAddr, pkt->id, pkt->allegiance, pkt->hunter,
            SimpleTimer::getPlatformClock()->milliseconds(), 0);
        remotePlayer = remotePlayers.end() - 1;
        LOG_I("RPM", "Added discovered player %s (Allegiance: %u, %s) at addr %X:%X:%X:%X:%X:%X\n", 
            remotePlayer->playerInfo.getUserID().c_str(),
            remotePlayer->playerInfo.getAllegiance(),
            remotePlayer->playerInfo.isHunter() ? "Hunter" : "Not Hunter",
            srcMacAddr[0], srcMacAddr[1], srcMacAddr[2],
            srcMacAddr[3], srcMacAddr[4], srcMacAddr[5]);
    }

    //If we have a local record of this player, update their last seen time, regardless of packet type
    if(remotePlayer != remotePlayers.end())
    {
        remotePlayer->lastSeenTime = SimpleTimer::getPlatformClock()->milliseconds();
#if PDN_ENABLE_RSSI_TRACKING
        remotePlayer->rssi = EspNowManager::GetInstance()->GetRssiForPeer(srcMacAddr);
        LOG_D("RPM", "Updated peer rssi to %i\n", remotePlayer->rssi);
#endif
    }
    return 0;
}
