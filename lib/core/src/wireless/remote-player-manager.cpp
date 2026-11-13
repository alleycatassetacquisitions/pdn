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

RemotePlayerManager::RemotePlayerManager(PeerCommsInterface* peerComms)
    : broadcastInterval(5000)
    , lastBroadcastTime(0) {
    this->peerComms = peerComms;
}

void RemotePlayerManager::Update()
{
    unsigned long now = SimpleTimer::getPlatformClock()->milliseconds();

    //Broadcast if we need to
    if( (lastBroadcastTime != 0) && (now - lastBroadcastTime >= broadcastInterval))
    {
        BroadcastPlayerInfo();
    }
}

int RemotePlayerManager::BroadcastPlayerInfo()
{
    PlayerInfoPkt broadcastPkt;
    IdGenerator::copyId(broadcastPkt.id, localPlayerInfo->getUserID().c_str());
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

    lastRssi = peerComms->getRssiForPeer(srcMacAddr);
    packetReceived = true;

    return 0;
}

bool RemotePlayerManager::consumePacketReceived()
{
    bool received = packetReceived;
    packetReceived = false;
    return received;
}

int RemotePlayerManager::getLastRssi() const { return lastRssi; }
