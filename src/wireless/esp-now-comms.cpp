#include <algorithm>
#include <Arduino.h> //For Serial, may be replaced with more specific header?
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>

#include "wireless/esp-now-comms.hpp"

#define DEBUG_PRINT_ESP_NOW 0


struct DataPktHdr
{
    //Total packet length including header
    uint8_t pktLen;
    PktType packetType;
    uint8_t numPktsInCluster;
    uint8_t idxInCluster;
} __attribute__((packed));

constexpr size_t MAX_PKT_DATA_SIZE = ESP_NOW_MAX_DATA_LEN - sizeof(DataPktHdr);

EspNowManager *EspNowManager::GetInstance()
{
    static EspNowManager instance;
    return &instance;
}

void EspNowManager::Update()
{
    //TODO: Is this actually needed anymore?
}

EspNowManager::EspNowManager() :
    m_pktHandlerCallbacks((int)PktType::kNumPacketTypes, std::pair<PktHandler, void*>(nullptr, nullptr)),
    m_maxRetries(5),
    m_curRetries(0)
{
    // Initialize WiFi first
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE("ENC", "WiFi init failed: 0x%X", err);
        return;
    }

    // Set WiFi mode to STA
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE("ENC", "WiFi set mode failed: 0x%X", err);
        return;
    }

    // Start WiFi
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE("ENC", "WiFi start failed: 0x%X", err);
        return;
    }

#if PDN_ENABLE_RSSI_TRACKING
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(EspNowManager::WifiPromiscuousRecvCallback);
    esp_wifi_set_promiscuous(true);
#endif

    // Now initialize ESP-NOW
    err = esp_now_init();
    if(err != ESP_OK) {
        ESP_LOGE("ENC", "ESPNOW failed to init: 0x%X", err);
        return;
    }

    //Register callbacks
    err = esp_now_register_recv_cb(EspNowManager::EspNowRecvCallback);
    if(err != ESP_OK) {
        ESP_LOGE("ENC", "ESPNOW Error registering recv cb: 0x%X", err);
        return;
    }
    
    err = esp_now_register_send_cb(EspNowManager::EspNowSendCallback);
    if(err != ESP_OK) {
        ESP_LOGE("ENC", "ESPNOW Error registering send cb: 0x%X", err);
        return;
    }

    //Register broadcast peer
    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, ESP_NOW_BROADCAST_ADDR, ESP_NOW_ETH_ALEN);
    err = esp_now_add_peer(&broadcastPeer);
    if(err != ESP_OK) {
        ESP_LOGE("ENC", "ESPNOW Error registering broadcast peer: 0x%X", err);
        return;
    }

    ESP_LOGI("ENC", "ESPNOW Comms initialized successfully");
}

#if PDN_ENABLE_RSSI_TRACKING
void EspNowManager::WifiPromiscuousRecvCallback(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;

    //TODO: Filter to make sure it's an Action frame
    
    //ESP-NOW uses category type 127 in vendor specific action frames (a type of mgmt frame)
    if(pkt->payload[24] != 127)
        return;
    
    int rssi = pkt->rx_ctrl.rssi;
    //2 bytes for frame control
    //2 bytes for duration id
    //6 bytes for first mac addr (which is receiver)
    //=10 byte offset to get to sender
    const uint8_t* srcMac = (pkt->payload) + 10;
    uint64_t srcMac64 = MacToUInt64(srcMac);

    EspNowManager::GetInstance()->m_rssiTracker[srcMac64] = rssi;
    
}
#endif

int EspNowManager::SendData(const uint8_t* dstMac, const PktType packetType, const uint8_t *data, size_t len)
{
    if(len > (255 * MAX_PKT_DATA_SIZE))
    {
        ESP_LOGW("ENC", "ESP-NOW: Tried to send too large of buffer: %u of max %u\n",
            len, 
            255 * MAX_PKT_DATA_SIZE);
        return -1;
    }

    //Calculate the total number of ESP_NOW_MAX_DATA_LEN (250) byte packets we'll 
    //need to send the whole buffer
    uint8_t numInCluster = len / MAX_PKT_DATA_SIZE + 
                           (len % MAX_PKT_DATA_SIZE == 0 ? 0 : 1);
    
    //Allocate entire send buffer up front so we don't fail an allocation part way through
    //the cluster
    //We'll use alloca for tracking the buffers while we set them up since this should
    //require a very small amount of memory making the risk of stack overflow very small
    //This will also be much faster than malloc/free which would be wasteful for such a
    //small allocation
    uint8_t** sendBuffers = (uint8_t**)alloca(sizeof(uint8_t*) * numInCluster);
    size_t bytesLeft = len;
    for(int i = 0; i < numInCluster; ++i)
    {
        size_t thisBuffer = std::min(bytesLeft, MAX_PKT_DATA_SIZE);
        sendBuffers[i] = (uint8_t*)ps_malloc(sizeof(DataPktHdr) + thisBuffer);
        if(!sendBuffers[i])
        {
            //free anything we already allocated
            for(int j = 0; j < i; ++j)
                free(sendBuffers[j]);

            //TODO: Return better error code once we have them
            ESP_LOGE("ENC", "Failed to allocate buffers for ESP-NOW send queue");
            ESP_LOGE("ENC", "Needed to allocate a total of %lu bytes\n", len);
            return -1;
        }
        bytesLeft -= thisBuffer;
    }

    //Before we start filling the send queue, see if we'll need to start
    //sending afterwards
    bool willNeedToStartSend = m_sendQueue.empty();

    //Build up each packet
    bytesLeft = len;
    for(int pktIdx = 0; pktIdx < numInCluster; ++pktIdx)
    {
        size_t thisBuffer = std::min(bytesLeft, MAX_PKT_DATA_SIZE);
#if DEBUG_PRINT_ESP_NOW
        ESP_LOGD("ENC", "ESPNOW SendData pktIdx: %i thisBuffer: %u\n", pktIdx, thisBuffer);
#endif

        //Each packet needs a header, build it directly in the send buffer
        DataPktHdr* hdr = (DataPktHdr*)sendBuffers[pktIdx];
        hdr->idxInCluster = pktIdx;
        hdr->numPktsInCluster = numInCluster;
        hdr->pktLen = sizeof(DataPktHdr) + thisBuffer;
        hdr->packetType = packetType;

        //Copy the actual data into the send buffer following the header
        size_t dataOffset = pktIdx * MAX_PKT_DATA_SIZE;
        memcpy(sendBuffers[pktIdx] + sizeof(DataPktHdr), data + dataOffset, thisBuffer);

        //Now add it to the send queue
        DataSendBuffer buffer;
        memcpy(buffer.dstMac, dstMac, ESP_NOW_ETH_ALEN);
        buffer.ptr = sendBuffers[pktIdx];
        buffer.len = hdr->pktLen;
        m_sendQueue.push(buffer);

        bytesLeft -= thisBuffer;
    }

    //Check if this is the first packet in the queue
    if(willNeedToStartSend)
    {
        SendFrontPkt();
    }
    return 0;
}

void EspNowManager::SetPacketHandler(const PktType packetType, PktHandler callback, void* userArg)
{
    m_pktHandlerCallbacks[(int)packetType].first = callback;
    m_pktHandlerCallbacks[(int)packetType].second = userArg;
}

void EspNowManager::ClearPacketHandler(const PktType packetType)
{
    m_pktHandlerCallbacks[(int)packetType].first = nullptr;
}

#if PDN_ENABLE_RSSI_TRACKING
int EspNowManager::GetRssiForPeer(const uint8_t *macAddr)
{
    uint64_t macAddr64 = MacToUInt64(macAddr);
    if(m_rssiTracker.count(macAddr64) > 0)
        return m_rssiTracker[macAddr64];
    return -1;
}
#endif

void EspNowManager::EspNowRecvCallback(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    EspNowManager* manager = EspNowManager::GetInstance();

#if DEBUG_PRINT_ESP_NOW
    ESP_LOGD("ENC", "ESPNOW Recv Callback len %i from %X:%X:%X:%X:%X:%X\n", data_len,
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5]);
#endif

    //Make sure received packet is at least min length
    if(data_len < sizeof(DataPktHdr))
    {
        ESP_LOGE("ENC", "Recieved buffer (%i bytes) was smaller than header (%u)\n", data_len, sizeof(DataPktHdr));
        return;
    }

    const DataPktHdr* pktHdr = (const DataPktHdr*)data;

#if DEBUG_PRINT_ESP_NOW
    ESP_LOGD("ENC", "Packet Type: %i\n", pktHdr->packetType);
#endif

    //Check for multipacket cluster
    if(pktHdr->numPktsInCluster > 1)
    {
        //Need a static array to use as map key
        //TODO: Is there a way to just cast mac_addr?
        uint64_t tmpMacAddr = MacToUInt64(mac_addr);
        //If this is the first packet, we'll need some special handling
        if(pktHdr->idxInCluster == 0)
        {
            //If there's an existing cluster for this mac address, release it
            //as that means we lost a packet somewhere
            auto existingBuffer = manager->m_recvBuffers.find(tmpMacAddr);
            if(existingBuffer != manager->m_recvBuffers.end())
            {
                free(existingBuffer->second.data);
                manager->m_recvBuffers.erase(existingBuffer);
            }

            DataRecvBuffer newBuffer;
            newBuffer.data = (uint8_t*)ps_malloc(pktHdr->numPktsInCluster * MAX_PKT_DATA_SIZE);
            newBuffer.expectedNextIdx = 1;
        }
        {
            auto existingBuffer = manager->m_recvBuffers.find(tmpMacAddr);
            //At this point, we should have a recv buffer for this mac address
            //if we don't, we missed the start packet in this cluster
            if(existingBuffer != manager->m_recvBuffers.end())
            {
                DataRecvBuffer recvBuffer = existingBuffer->second;
                if(pktHdr->idxInCluster != recvBuffer.expectedNextIdx)
                {
                    ESP_LOGW("ENC", "Received pkt %u when expecting %u. Must have missed a packet in cluster.\n",
                                  recvBuffer.expectedNextIdx,
                                  pktHdr->idxInCluster);
                    free(recvBuffer.data);
                    manager->m_recvBuffers.erase(existingBuffer);
                    return;
                }

                //Copy data into our recv buffer
                size_t bufferOffset = pktHdr->idxInCluster * MAX_PKT_DATA_SIZE;
                memcpy(recvBuffer.data + bufferOffset, data + sizeof(DataPktHdr), pktHdr->pktLen - sizeof(DataPktHdr));
                ++existingBuffer->second.expectedNextIdx;
                
                //Check for this being the last packet in cluster
                if(existingBuffer->second.expectedNextIdx == pktHdr->numPktsInCluster)
                {
                    size_t totalClusterSize = (pktHdr->numPktsInCluster - 1) * MAX_PKT_DATA_SIZE;
                    totalClusterSize += pktHdr->pktLen - sizeof(DataPktHdr);
                    manager->HandlePktCallback(pktHdr->packetType, mac_addr, recvBuffer.data, totalClusterSize);
                    free(recvBuffer.data);
                    manager->m_recvBuffers.erase(existingBuffer);
                    return;
                }
            }
            else
            {
                ESP_LOGW("ENC", "No recv buffer for mid cluster pkt. We must have missed first pkt.");
                return;
            }
        }
    }
    else
    {
        //Single packet cluster
        manager->HandlePktCallback(pktHdr->packetType, mac_addr, data + sizeof(DataPktHdr), pktHdr->pktLen - sizeof(DataPktHdr));
    }
}

void EspNowManager::EspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    EspNowManager* manager = EspNowManager::GetInstance();

#if DEBUG_PRINT_ESP_NOW
    ESP_LOGD("ENC", "ESPNOW Send Callback");
#endif

    if(status == ESP_NOW_SEND_SUCCESS)
    {
        manager->MoveToNextSendPkt();
    }
    else
    {
        if(manager->m_curRetries < manager->m_maxRetries)
        {
            ESP_LOGW("ENC", "ESPNOW Failed send, retrying");
            ++manager->m_curRetries;
        }
        else
        {
            ESP_LOGE("ENC", "ESPNOW Failed send, giving up");
            manager->MoveToNextSendPkt();
        }
    }

    if(!manager->m_sendQueue.empty())
    {
        //TODO: Catch error and do reporting and push to next pkt
        manager->SendFrontPkt();
    }
}

int EspNowManager::SendFrontPkt()
{
    DataSendBuffer buffer = m_sendQueue.front();
    
    //If this is the first packet in cluster, make sure the peer is registered
    DataPktHdr* hdr = (DataPktHdr*)buffer.ptr;
    if(hdr->idxInCluster == 0 && (memcmp(buffer.dstMac, ESP_NOW_BROADCAST_ADDR, ESP_NOW_ETH_ALEN) != 0))
        EnsurePeerIsRegistered(buffer.dstMac);

    esp_err_t err;
    do
    {
        err = esp_now_send(buffer.dstMac, buffer.ptr, buffer.len);
        if(err != ESP_OK)
        {
            ++m_curRetries;
            if(m_curRetries >= m_maxRetries)
            {
                ESP_LOGE("ENC", "ESPNOW Failed after max retries. Err: %i\n", err);
                //TODO: Pop all packets in the current cluster?
                MoveToNextSendPkt();
                if(!m_sendQueue.empty())
                    SendFrontPkt();
                //TODO: Return correct error code
                return -1;
            }
        }
    } while (err != ESP_OK);
    
    return 0;
}

void EspNowManager::MoveToNextSendPkt()
{
        free(m_sendQueue.front().ptr);
        m_sendQueue.pop();
        m_curRetries = 0;
}

int EspNowManager::EnsurePeerIsRegistered(const uint8_t *mac_addr)
{
    //Peer already registered
    if(esp_now_is_peer_exist(mac_addr))
        return 0;

    esp_now_peer_num_t num_peers;
    esp_now_get_peer_num(&num_peers);
    if(num_peers.total_num > 19)
    {
        //We need to remove one, for now just remove a random peer
        esp_now_peer_info_t rand_peer;
        esp_now_fetch_peer(false, &rand_peer);

        esp_now_del_peer(rand_peer.peer_addr);
    }

    esp_now_peer_info_t new_peer = {};
    memcpy(new_peer.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
    esp_now_add_peer(&new_peer);

    return 0;
}

void EspNowManager::HandlePktCallback(const PktType packetType, const uint8_t *srcMacAddr, const uint8_t *pktData, const size_t pktLen)
{
    if((int)packetType >= (int)PktType::kNumPacketTypes)
    {
        ESP_LOGE("ENC", "Recv invalid packet type: %u\n", (int)packetType);
        return;
    }

    PktHandler callback = m_pktHandlerCallbacks[(int)packetType].first;
    if(callback)
    {
        callback(srcMacAddr, pktData, pktLen, m_pktHandlerCallbacks[(int)packetType].second);
    }
}

EspNowManager::~EspNowManager() {
    // Cleanup ESP-NOW
    esp_now_deinit();
    
    // Cleanup WiFi
    esp_wifi_stop();
    esp_wifi_deinit();
    
    ESP_LOGI("ENC", "ESPNOW Comms cleaned up");
}
