#include "wireless/remote-player-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "utils/simple-timer.hpp"

#define DEBUG_REMOTE_PLAYER_MANAGER 0

RemotePlayerManager::RemotePlayerManager(PeerCommsInterface* peerComms, DeviceType deviceType)
    : peerComms(peerComms)
    , deviceType(deviceType)
    , broadcastInterval(5000) {}

void RemotePlayerManager::update() {
    if (localPlayerInfo == nullptr) return;
    if (broadcastTimer.isRunning() && !broadcastTimer.expired()) return;
    BroadcastPlayerInfo();
}

int RemotePlayerManager::BroadcastPlayerInfo()
{
    PlayerInfoPkt broadcastPkt;
    IdGenerator::copyId(broadcastPkt.id, localPlayerInfo->getUserID().c_str());
    broadcastPkt.allegiance = localPlayerInfo->getAllegiance();
    broadcastPkt.hunter = localPlayerInfo->isHunter();
    broadcastPkt.senderDeviceType = static_cast<uint8_t>(deviceType);

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
    // The driver refuses while the FDN has handed the radio to Wi-Fi and while
    // its send queue is full. A refused beacon is offered again after a short
    // wait rather than every tick, and is not counted as sent.
    broadcastTimer.setTimer(ret >= 0 ? broadcastInterval : BEACON_RETRY_AFTER_REFUSAL_MS);
    return ret;
}

void RemotePlayerManager::startBroadcastingPlayerInfo(Player* playerInfo,
                                                      unsigned long broadcastIntervalMillis) {
    localPlayerInfo = playerInfo;
    broadcastInterval = broadcastIntervalMillis;
    broadcastTimer.invalidate();
    update();
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
