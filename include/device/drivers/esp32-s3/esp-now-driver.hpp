#pragma once

#include <algorithm>
#include <vector>
#include <queue>
#include <unordered_map>
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include "device/drivers/logger.hpp"
#include "device/drivers/driver-interface.hpp"
#include "wireless/mac-functions.hpp"
#include "wireless/peer-comms-types.hpp"
#include "device/device-constants.hpp"

#define DEBUG_PRINT_ESP_NOW 0

//Change to 1 to enable tracking rssi for peers
//This works, but requires wifi to be in promiscuous mode
//which likely prevents connecting to access points and
//requires an unknown but likely high amount of processing
//power
#define PDN_ENABLE_RSSI_TRACKING 0

//Use this mac address in order to reach all nearby devices
constexpr uint8_t PEER_BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

constexpr size_t MAX_PKT_DATA_SIZE = ESP_NOW_MAX_DATA_LEN - sizeof(DataPktHdr);

//Singleton class that handles communication over ESP-NOW protocol.
class EspNowManager : public PeerCommsDriverInterface
{
public:
    static EspNowManager* CreateEspNowManager(const std::string& name) {
        instance = new EspNowManager(name);
        return instance;
    }

    static EspNowManager* GetInstance() {
        return instance;
    }

    // === PEER COMMS INTERFACE === //

    void exec() override {
        // ESP-NOW uses interrupt-driven callbacks, no polling needed
    }

    void connect() override {
        // Set WiFi to station mode
        WiFi.mode(WIFI_STA);

        // Disconnect from any AP but keep WiFi radio ON (false = keep radio running)
        // ESP-NOW requires the WiFi radio to be active!
        WiFi.disconnect(false);

        // Small delay to let WiFi stabilize after mode change
        delay(100);

        // Set the channel using ESP-IDF API for reliability
        esp_err_t err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
            LOG_E("ENC", "Failed to set channel %d: %s", ESPNOW_CHANNEL, esp_err_to_name(err));
        }

        // Verify the channel was set correctly
        uint8_t primary_channel;
        wifi_second_chan_t secondary_channel;
        esp_wifi_get_channel(&primary_channel, &secondary_channel);
        LOG_I("ENC", "WiFi channel set to: %d (requested: %d)", primary_channel, ESPNOW_CHANNEL);

        initializeEspNow();
        peerCommsState = PeerCommsState::CONNECTED;
    }

    void disconnect() override {
        esp_err_t err = esp_now_deinit();
        if(err != ESP_OK) {
            LOG_E("ENC", "ESPNOW Error deinitializing: 0x%X\n", err);
            return;
        }

        peerCommsState = PeerCommsState::DISCONNECTED;
    }

    PeerCommsState getPeerCommsState() override {
        return peerCommsState;
    }

    void setPeerCommsState(PeerCommsState state) override {
        if(state == PeerCommsState::CONNECTED && peerCommsState != PeerCommsState::CONNECTED) {
            connect();
        }
        else if(state == PeerCommsState::DISCONNECTED && peerCommsState != PeerCommsState::DISCONNECTED) {
            disconnect();
        }
    }

    //Queues up data for sending, may not send right away
    int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) override {
        if(length > (255 * MAX_PKT_DATA_SIZE))
        {
            LOG_W("ENC", "ESP-NOW: Tried to send too large of buffer: %u of max %u\n",
                length,
                255 * MAX_PKT_DATA_SIZE);
            return -1;
        }

        //Calculate the total number of ESP_NOW_MAX_DATA_LEN (250) byte packets we'll
        //need to send the whole buffer
        uint8_t numInCluster = length / MAX_PKT_DATA_SIZE +
                               (length % MAX_PKT_DATA_SIZE == 0 ? 0 : 1);

        //Allocate entire send buffer up front so we don't fail an allocation part way through
        //the cluster
        //We'll use alloca for tracking the buffers while we set them up since this should
        //require a very small amount of memory making the risk of stack overflow very small
        //This will also be much faster than malloc/free which would be wasteful for such a
        //small allocation
        auto sendBuffers = static_cast<uint8_t**>(alloca(sizeof(uint8_t*) * numInCluster));
        size_t bytesLeft = length;
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
                LOG_E("ENC", "Failed to allocate buffers for ESP-NOW send queue");
                LOG_E("ENC", "Needed to allocate a total of %lu bytes\n", length);
                return -1;
            }
            bytesLeft -= thisBuffer;
        }

        //Before we start filling the send queue, see if we'll need to start
        //sending afterwards
        bool willNeedToStartSend = m_sendQueue.empty();

        //Build up each packet
        bytesLeft = length;
        for(int pktIdx = 0; pktIdx < numInCluster; ++pktIdx)
        {
            size_t thisBuffer = std::min(bytesLeft, MAX_PKT_DATA_SIZE);
#if DEBUG_PRINT_ESP_NOW
            ESP_LOGD("ENC", "ESPNOW SendData pktIdx: %i thisBuffer: %u\n", pktIdx, thisBuffer);
#endif

            //Each packet needs a header, build it directly in the send buffer
            auto* hdr = reinterpret_cast<DataPktHdr*>(sendBuffers[pktIdx]);
            hdr->idxInCluster = pktIdx;
            hdr->numPktsInCluster = numInCluster;
            hdr->pktLen = sizeof(DataPktHdr) + thisBuffer;
            hdr->packetType = packetType;

            //Copy the actual data into the send buffer following the header
            size_t dataOffset = pktIdx * MAX_PKT_DATA_SIZE;
            memcpy(sendBuffers[pktIdx] + sizeof(DataPktHdr), data + dataOffset, thisBuffer);

            //Now add it to the send queue
            DataSendBuffer buffer;
            memcpy(buffer.dstMac, dst, ESP_NOW_ETH_ALEN);
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

    //Set the packet handler for a particular packet type
    //Only one handler can be registered per packet type at a time, so if a new
    //packet handler is registered for a packet type that has an existing handler,
    //the existing handler is automatically unregistered
    //userArg will be saved per packet type and will be passed in unmodified to
    //packet handler for that packet type (when a packet of that type is receieved)
    void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) override {
        m_pktHandlerCallbacks[(int)packetType].first = callback;
        m_pktHandlerCallbacks[(int)packetType].second = ctx;
    }

    //Unregister packet handler for specified packet type
    void clearPacketHandler(PktType packetType) override {
        m_pktHandlerCallbacks[(int)packetType].first = nullptr;
    }

    // Called by DriverManager at startup - we don't initialize ESP-NOW here
    // because WiFi must be set up first. Actual init happens in connect().
    int initialize() override {
        // No-op: ESP-NOW initialization requires WiFi to be running first.
        // The actual initialization happens in connect() -> initializeEspNow()
        return 0;
    }

#if PDN_ENABLE_RSSI_TRACKING
    int GetRssiForPeer(const uint8_t* macAddr) {
        uint64_t macAddr64 = MacToUInt64(macAddr);
        if(m_rssiTracker.count(macAddr64) > 0)
            return m_rssiTracker[macAddr64];
        return -1;
    }
#endif

    // Public methods for ESP-NOW callback handling
    // (used when re-initializing ESP-NOW in EspNowState)
    void HandleReceivedData(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
        // This simply forwards to the static callback method
        EspNowRecvCallback(esp_now_info, data, data_len);
    }

    void HandleSendStatus(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status) {
        // This simply forwards to the static callback method
        EspNowSendCallback(esp_now_info, status);
    }

private:
    static EspNowManager* instance;

    // Struct definitions must come before methods that use them
    struct DataSendBuffer
    {
        uint8_t dstMac[6];
        uint8_t* ptr;
        size_t len;
    };

    struct DataRecvBuffer
    {
        uint8_t* data = nullptr;
        unsigned long mostRecentRecvPktTime = 0;
        uint8_t expectedNextIdx = 0;
    };

    explicit EspNowManager(const std::string& name) :
        PeerCommsDriverInterface(name),
        m_pktHandlerCallbacks((int)PktType::kNumPacketTypes, std::pair<PacketCallback, void*>(nullptr, nullptr)),
        m_maxRetries(5),
        m_curRetries(0)
    {
#if PDN_ENABLE_RSSI_TRACKING
        wifi_promiscuous_filter_t filter = {
            .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
        esp_wifi_set_promiscuous_filter(&filter);
        esp_wifi_set_promiscuous_rx_cb(EspNowManager::WifiPromiscuousRecvCallback);
        esp_wifi_set_promiscuous(true);
#endif
        // ESP-NOW initialization happens in connect() -> initializeEspNow()
        // after WiFi has been set up
    }

    // Actually initializes ESP-NOW - must be called after WiFi is configured
    int initializeEspNow() {
        // Initialize ESP-NOW
        esp_err_t err = esp_now_init();
        if(err != ESP_OK)
        {
            LOG_E("ENC", "ESPNOW failed to init: 0x%X\n", err);
            return -1;
        }

        // Register callbacks
        err = esp_now_register_recv_cb(EspNowManager::EspNowRecvCallback);
        if(err != ESP_OK) {
            LOG_E("ENC", "ESPNOW Error registering recv cb: 0x%X\n", err);
            return -1;
        }

        err = esp_now_register_send_cb(EspNowManager::EspNowSendCallback);
        if(err != ESP_OK) {
            LOG_E("ENC", "ESPNOW Error registering send cb: 0x%X\n", err);
            return -1;
        }

        // Register broadcast peer
        esp_now_peer_info_t broadcastPeer = {};
        memcpy(broadcastPeer.peer_addr, PEER_BROADCAST_ADDR, ESP_NOW_ETH_ALEN);
        err = esp_now_add_peer(&broadcastPeer);

        if(err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
            LOG_E("ENC", "ESPNOW Error registering broadcast peer: 0x%X\n", err);
            return err;
        }

        LOG_I("ENC", "ESPNOW initialization complete");

        return 0;
    }

#if PDN_ENABLE_RSSI_TRACKING
    //Callback for receiving raw Wifi packets, used for rssi tracking
    static void WifiPromiscuousRecvCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
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

    //ESP-NOW callbacks
    static void EspNowRecvCallback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
        EspNowManager* manager = EspNowManager::GetInstance();

#if DEBUG_PRINT_ESP_NOW
        ESP_LOGD("ENC", "ESPNOW Recv Callback len %i from %X:%X:%X:%X:%X:%X\n", data_len,
            esp_now_info->src_addr[0], esp_now_info->src_addr[1], esp_now_info->src_addr[2],
            esp_now_info->src_addr[3], esp_now_info->src_addr[4], esp_now_info->src_addr[5]);
#endif

        if(data_len < sizeof(DataPktHdr)) {
            LOG_E("ENC", "Received buffer (%i bytes) was smaller than header (%u)\n", data_len, sizeof(DataPktHdr));
            return;
        }

        const auto* pktHdr = reinterpret_cast<const DataPktHdr*>(data);

#if DEBUG_PRINT_ESP_NOW
        ESP_LOGD("ENC", "Packet Type: %i\n", pktHdr->packetType);
#endif

        if(pktHdr->numPktsInCluster > 1) {
            manager->handleMultiPacketCluster(esp_now_info->src_addr, data, pktHdr);
        } else {
            manager->handleSinglePacket(esp_now_info->src_addr, data, pktHdr);
        }
    }

    // Helper methods for packet reception
    void handleSinglePacket(const uint8_t* mac_addr, const uint8_t* data, const DataPktHdr* pktHdr) {
        HandlePktCallback(pktHdr->packetType, mac_addr, data + sizeof(DataPktHdr), pktHdr->pktLen - sizeof(DataPktHdr));
    }

    void handleMultiPacketCluster(const uint8_t* mac_addr, const uint8_t* data, const DataPktHdr* pktHdr) {
        uint64_t macAddr64 = MacToUInt64(mac_addr);

        if(pktHdr->idxInCluster == 0) {
            handleFirstPacketInCluster(macAddr64, pktHdr);
        }

        handleSubsequentPacket(mac_addr, data, pktHdr, macAddr64);
    }

    void handleFirstPacketInCluster(uint64_t macAddr64, const DataPktHdr* pktHdr) {
        // If there's an existing cluster for this mac address, release it
        // as that means we lost a packet somewhere
        auto existingBuffer = m_recvBuffers.find(macAddr64);
        if(existingBuffer != m_recvBuffers.end()) {
            free(existingBuffer->second.data);
            m_recvBuffers.erase(existingBuffer);
        }

        DataRecvBuffer newBuffer;
        newBuffer.data = (uint8_t*)ps_malloc(pktHdr->numPktsInCluster * MAX_PKT_DATA_SIZE);
        newBuffer.expectedNextIdx = 1;
        m_recvBuffers[macAddr64] = newBuffer;
    }

    void handleSubsequentPacket(const uint8_t* mac_addr, const uint8_t* data, const DataPktHdr* pktHdr, uint64_t macAddr64) {
        auto existingBuffer = m_recvBuffers.find(macAddr64);

        // If no buffer exists, we missed the start packet
        if(existingBuffer == m_recvBuffers.end()) {
            LOG_W("ENC", "No recv buffer for mid cluster pkt. We must have missed first pkt.");
            return;
        }

        DataRecvBuffer& recvBuffer = existingBuffer->second;

        if(!validatePacketSequence(pktHdr, recvBuffer, existingBuffer)) {
            return;
        }

        copyPacketData(data, pktHdr, recvBuffer);
        ++existingBuffer->second.expectedNextIdx;

        if(isClusterComplete(existingBuffer->second, pktHdr)) {
            finalizeCluster(mac_addr, pktHdr, recvBuffer, existingBuffer);
        }
    }

    bool validatePacketSequence(const DataPktHdr* pktHdr, DataRecvBuffer& recvBuffer,
                                std::unordered_map<uint64_t, DataRecvBuffer>::iterator& existingBuffer) {
        if(pktHdr->idxInCluster != recvBuffer.expectedNextIdx) {
            LOG_W("ENC", "Received pkt %u when expecting %u. Must have missed a packet in cluster.\n",
                  recvBuffer.expectedNextIdx, pktHdr->idxInCluster);
            free(recvBuffer.data);
            m_recvBuffers.erase(existingBuffer);
            return false;
        }
        return true;
    }

    void copyPacketData(const uint8_t* data, const DataPktHdr* pktHdr, DataRecvBuffer& recvBuffer) {
        size_t bufferOffset = pktHdr->idxInCluster * MAX_PKT_DATA_SIZE;
        memcpy(recvBuffer.data + bufferOffset, data + sizeof(DataPktHdr), pktHdr->pktLen - sizeof(DataPktHdr));
    }

    bool isClusterComplete(const DataRecvBuffer& recvBuffer, const DataPktHdr* pktHdr) {
        return recvBuffer.expectedNextIdx == pktHdr->numPktsInCluster;
    }

    void finalizeCluster(const uint8_t* mac_addr, const DataPktHdr* pktHdr, DataRecvBuffer& recvBuffer,
                         std::unordered_map<uint64_t, DataRecvBuffer>::iterator& existingBuffer) {
        size_t totalClusterSize = (pktHdr->numPktsInCluster - 1) * MAX_PKT_DATA_SIZE;
        totalClusterSize += pktHdr->pktLen - sizeof(DataPktHdr);

        HandlePktCallback(pktHdr->packetType, mac_addr, recvBuffer.data, totalClusterSize);

        free(recvBuffer.data);
        m_recvBuffers.erase(existingBuffer);
    }

    static void EspNowSendCallback(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status) {
        EspNowManager* manager = EspNowManager::GetInstance();

#if DEBUG_PRINT_ESP_NOW
        ESP_LOGD("ENC", "ESPNOW Send Callback");
#endif

        if(status == ESP_NOW_SEND_SUCCESS)
        {
            LOG_D("ENC", "Send SUCCESS");
            manager->MoveToNextSendPkt();
        }
        else
        {
            if(manager->m_curRetries < manager->m_maxRetries)
            {
                LOG_W("ENC", "Send FAILED (retry %d/%d)",
                      manager->m_curRetries + 1, manager->m_maxRetries);
                ++manager->m_curRetries;
            }
            else
            {
                LOG_E("ENC", "Send FAILED - giving up after %d retries",
                      manager->m_maxRetries);
                manager->MoveToNextSendPkt();
            }
        }

        if(!manager->m_sendQueue.empty())
        {
            //TODO: Catch error and do reporting and push to next pkt
            manager->SendFrontPkt();
        }
    }

    //Attempt to send the next packet in send queue
    int SendFrontPkt() {
        auto buffer = m_sendQueue.front();

        //If this is the first packet in cluster, make sure the peer is registered
        auto* hdr = reinterpret_cast<DataPktHdr*>(buffer.ptr);
        if(hdr->idxInCluster == 0 && (memcmp(buffer.dstMac, PEER_BROADCAST_ADDR, ESP_NOW_ETH_ALEN) != 0))
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
                    LOG_E("ENC", "ESPNOW Failed after max retries. Err: %i\n", err);
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

    //Free front packet in send queue and pop it from queue
    void MoveToNextSendPkt() {
        free(m_sendQueue.front().ptr);
        m_sendQueue.pop();
        m_curRetries = 0;
    }

    //ESP-NOW requires peers to be registered but there's a limit,
    //so we'll need to unregister least recently used peer if we
    //hit the limit. This function will ensure a peer is registered.
    int EnsurePeerIsRegistered(const uint8_t* mac_addr) {
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
        new_peer.channel = 0;  // 0 = current channel
        new_peer.ifidx = WIFI_IF_STA;  // Use STA interface
        new_peer.encrypt = false;

        esp_err_t err = esp_now_add_peer(&new_peer);
        if (err != ESP_OK) {
            LOG_E("ENC", "Failed to add peer: 0x%X", err);
            return -1;
        }

        LOG_I("ENC", "Added peer: %02X:%02X:%02X:%02X:%02X:%02X",
              mac_addr[0], mac_addr[1], mac_addr[2],
              mac_addr[3], mac_addr[4], mac_addr[5]);

        return 0;
    }

    //Storage for packet handler callbacks and their user args
    std::vector<std::pair<PacketCallback, void*>> m_pktHandlerCallbacks;

    //Handle received packet of a certain type
    void HandlePktCallback(const PktType packetType, const uint8_t* srcMacAddr, const uint8_t* pktData, const size_t pktLen) {
        if((int)packetType >= (int)PktType::kNumPacketTypes)
        {
            LOG_E("ENC", "Recv invalid packet type: %u\n", (int)packetType);
            return;
        }

        PacketCallback callback = m_pktHandlerCallbacks[(int)packetType].first;
        if(callback)
        {
            callback(srcMacAddr, pktData, pktLen, m_pktHandlerCallbacks[(int)packetType].second);
        }
    }

    uint8_t* getMacAddress() override {
        esp_read_mac(macAddress_, ESP_MAC_WIFI_STA);
        return macAddress_;
    }

    const uint8_t* getGlobalBroadcastAddress() override {
        return PEER_BROADCAST_ADDR;
    }

    void removePeer(uint8_t* macAddr) override {
        esp_now_del_peer(macAddr);
    }

    PeerCommsState peerCommsState = PeerCommsState::DISCONNECTED;

    //Storage for MAC address
    uint8_t macAddress_[6];

    //Storage for retry handling
    uint8_t m_maxRetries;
    uint8_t m_curRetries;

    //Packet send queue
    std::queue<DataSendBuffer> m_sendQueue;

    //Receive buffer tracking for multi-packet clusters
    std::unordered_map<uint64_t, DataRecvBuffer> m_recvBuffers;

#if PDN_ENABLE_RSSI_TRACKING
    //Storage for rssi, which is captured by wifi promiscuous callback
    std::unordered_map<uint64_t, int> m_rssiTracker;
#endif
};
