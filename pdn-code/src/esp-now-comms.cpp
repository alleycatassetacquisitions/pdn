#include <algorithm>
#include <esp_now.h>

#include "../include/esp-now-comms.hpp"

#define DEBUG_PRINT_ESP_NOW 0

const uint8_t BROADCAST_ADDR[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum class PktType : uint8_t
{
    kPlayerInfoBroadcast = 1,
};

struct DataPktHdr
{
    //Total packet length including header
    uint8_t pktLen;
    PktType packetType;
    uint8_t numPktsInCluster;
    uint8_t idxInCluster;
} __attribute__((packed));

struct PlayerInfoPkt
{
    DataPktHdr hdr;
    char id[32];
    Allegiance allegiance;
    uint8_t hunter;
} __attribute__((packed));

EspNowManager *EspNowManager::GetInstance()
{
    static EspNowManager instance;
    return &instance;
}

void EspNowManager::StartBroadcastingPlayerInfo(Player *playerInfo, unsigned long broadcastIntervalMillis)
{
    m_localPlayerInfo = playerInfo;
    m_broadcastInterval = broadcastIntervalMillis;
    BroadcastPlayerInfo();
}

void EspNowManager::SetRemotePlayerTTL(unsigned long ttl)
{
    m_remotePlayerTTL = ttl;
}

unsigned long EspNowManager::GetRemotePlayerTTL()
{
    return m_remotePlayerTTL;
}

void EspNowManager::Update()
{
    unsigned long now = millis();

    //Remove players not seen in a long time
    //Single pass removal using Erase-move idiom
    m_remotePlayers.erase(
        std::remove_if(m_remotePlayers.begin(), m_remotePlayers.end(),
            [&now,this](RemotePlayer p){ return now - p.lastSeenTime > m_remotePlayerTTL; }),
        m_remotePlayers.end());

    if(now - m_lastBroadcastTime >= m_broadcastInterval)
    {
        BroadcastPlayerInfo();
    }
}

EspNowManager::EspNowManager() :
    m_remotePlayerTTL(5000)
{
    //Initialize Wifi + ESP-NOW
    esp_err_t err = esp_now_init();
    if(err != ESP_OK)
    {
        Serial.printf("ESPNOW failed to init: 0x%X\n", err);
    }
    else
    {
        //Register callbacks
        esp_err_t err = esp_now_register_recv_cb(EspNowManager::EspNowRecvCallback);
        if(err != ESP_OK)
            Serial.printf("ESPNOW Error registering recv cb: 0x%X\n", err);
        err = esp_now_register_send_cb(EspNowManager::EspNowSendCallback);
        if(err != ESP_OK)
            Serial.printf("ESPNOW Error registering send cb: 0x%X\n", err);

        //Register broadcast peer
        esp_now_peer_info_t broadcastPeer = {};
        memcpy(broadcastPeer.peer_addr, BROADCAST_ADDR, ESP_NOW_ETH_ALEN);
        err = esp_now_add_peer(&broadcastPeer);
        if(err != ESP_OK)
            Serial.printf("ESPNOW Error registering broadcast peer: 0x%X\n", err);

        Serial.println("ESPNOW Comms initialized");
    }
}

int EspNowManager::BroadcastPlayerInfo()
{
    PlayerInfoPkt broadcastPkt;
    broadcastPkt.hdr.pktLen = sizeof(PlayerInfoPkt);
    broadcastPkt.hdr.packetType = PktType::kPlayerInfoBroadcast;
    broadcastPkt.hdr.numPktsInCluster = 1;
    broadcastPkt.hdr.idxInCluster = 0;
    strncpy(broadcastPkt.id, m_localPlayerInfo->getUserID().c_str(), 32);
    broadcastPkt.id[31] = '\0'; //TODO: Should be 33 bytes since ID could be 32 chars
    broadcastPkt.allegiance = m_localPlayerInfo->getAllegiance();
    broadcastPkt.hunter = m_localPlayerInfo->isHunter();

#if DEBUG_PRINT_ESP_NOW
    Serial.printf("Broadcasting player info. ID: %s Allegiance: %u %s (pktsize: %lu)\n",
                  broadcastPkt.id,
                  broadcastPkt.allegiance,
                  broadcastPkt.hunter ? "Hunter" : "Not Hunter",
                  sizeof(broadcastPkt));
#endif

    int ret = SendData(BROADCAST_ADDR, (uint8_t*)&broadcastPkt, sizeof(broadcastPkt));
    m_lastBroadcastTime = millis();
    return ret;
}

int EspNowManager::SendData(const uint8_t* dstMac, const uint8_t *data, size_t len)
{
    //TODO: If len > 250 we need to split up buffer into multiple packets
    
    DataSendBuffer buffer;
    buffer.ptr = (uint8_t*)malloc(len);
    if(!buffer.ptr)
    {
        //TODO: Change to use error logging and return better error code once we have them
        //Serial.println("Failed to allocate buffer for ESP-NOW send queue");
        return -1;
    }
    memcpy(buffer.dstMac, dstMac, ESP_NOW_ETH_ALEN);
    memcpy(buffer.ptr, data, len);
    buffer.len = len;
    m_sendQueue.push(buffer);

    //Check if this is the first packet in the queue
    if(m_sendQueue.size() == 1)
    {
        esp_err_t err = esp_now_send(buffer.dstMac, buffer.ptr, buffer.len);
        if(err != ESP_OK)
        {
            Serial.printf("ESPNOW failed send: 0x%X\n", err);
            //TODO: Return real error and retry
            return -1;
        }
    }
    return 0;
}

void EspNowManager::EspNowRecvCallback(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    EspNowManager* manager = EspNowManager::GetInstance();

#if DEBUG_PRINT_ESP_NOW
    Serial.printf("ESPNOW Recv Callback len %i from %X:%X:%X:%X:%X:%X\n", data_len,
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5]);

    // for(int i = 0; i < data_len; ++i)
    //     Serial.printf("0x%X ", data[i]);
    // Serial.printf("\n");
#endif

    //Make sure received packet is at least min length
    if(data_len < sizeof(DataPktHdr))
    {
        Serial.printf("[ESPNOW] Recieved buffer (%i bytes) was smaller than header (%u)\n", data_len, sizeof(DataPktHdr));
        return;
    }

    const DataPktHdr* pktHdr = (const DataPktHdr*)data;
    //auto rssi = esp_now_info->rx_ctrl->rssi;

#if DEBUG_PRINT_ESP_NOW
    Serial.printf("Packet Type: %i\n", pktHdr->packetType);
#endif

    //Find our remote player record in local list
    auto remotePlayer = std::find_if(manager->m_remotePlayers.begin(), manager->m_remotePlayers.end(),
        [mac_addr](RemotePlayer rp) { return memcmp(mac_addr, rp.wifiMacAddr, ESP_NOW_ETH_ALEN) == 0;});

    //If this is a player info pkt and we don't currently have a record for them, add them
    if(pktHdr->packetType == PktType::kPlayerInfoBroadcast && remotePlayer == manager->m_remotePlayers.end())
    {
        const PlayerInfoPkt* pkt = (const PlayerInfoPkt*)data;

#if DEBUG_PRINT_ESP_NOW
        Serial.printf("Discovered player %s Allegiance: %u IsHunter: %u\n", pkt->id, pkt->allegiance, pkt->hunter);
#endif

        manager->m_remotePlayers.emplace_back(mac_addr, pkt->id, pkt->allegiance, pkt->hunter,
            millis(), 0);
        remotePlayer = manager->m_remotePlayers.end() - 1;
        Serial.printf("Added discovered player %s (Allegiance: %u, %s) at addr %X:%X:%X:%X:%X:%X\n", 
            remotePlayer->playerInfo.getUserID().c_str(),
            remotePlayer->playerInfo.getAllegiance(),
            remotePlayer->playerInfo.isHunter() ? "Hunter" : "Not Hunter",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]);
    }

    //If we have a local record of this player, update their last seen time, regardless of packet type
    if(remotePlayer != manager->m_remotePlayers.end())
    {
        remotePlayer->lastSeenTime = millis();
    }
}

void EspNowManager::EspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    EspNowManager* manager = EspNowManager::GetInstance();

#if DEBUG_PRINT_ESP_NOW
    Serial.println("ESPNOW Send Callback");
#endif

    if(status == ESP_NOW_SEND_SUCCESS)
    {
        free(manager->m_sendQueue.front().ptr);
        manager->m_sendQueue.pop();

        if(!manager->m_sendQueue.empty())
        {
            DataSendBuffer buffer = manager->m_sendQueue.front();
            esp_err_t err = esp_now_send(buffer.dstMac, buffer.ptr, buffer.len);
            if(err != ESP_OK)
            {
                Serial.printf("ESPNOW failed send: 0x%X\n", err);
                //TODO: Retry?
            }
        }
    }
    else
    {
        Serial.println("Failed to send packet, retrying");
        DataSendBuffer buffer = manager->m_sendQueue.front();
        esp_err_t err = esp_now_send(buffer.dstMac, buffer.ptr, buffer.len);
        if(err != ESP_OK)
        {
            Serial.printf("ESPNOW failed send: 0x%X\n", err);
            //TODO: Retry?
        }
    }
}
