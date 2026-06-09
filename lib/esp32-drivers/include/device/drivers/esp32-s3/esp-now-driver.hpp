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
#include "device/drivers/peer-comms-types.hpp"
#include "esp32-driver-constants.hpp"

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
class EspNowDriver : public PeerCommsDriverInterface
{
public:
    static EspNowDriver* CreateEspNowDriver(const std::string& name) {
        instance = new EspNowDriver(name);
        return instance;
    }

    static EspNowDriver* GetInstance() {
        return instance;
    }

    // === PEER COMMS INTERFACE === //

    void exec() override {
        // Re-pin armed by connect(): retry (paced) until the readback sticks,
        // re-issuing the disconnect each attempt to kill whatever STA attempt
        // is blocking the pin. Stops if an excursion takes the radio.
        if (channelPinPending_ && peerCommsState == PeerCommsState::CONNECTED) {
            unsigned long now = millis();
            if (now - lastChannelPinMs_ >= kChannelPinRetryMs) {
                lastChannelPinMs_ = now;
                WiFi.disconnect(false);
                if (pinEspNowChannel()) {
                    channelPinPending_ = false;
                    LOG_W("ENC", "channel pin recovered: on %d", ESPNOW_CHANNEL);
                }
            }
        }

        std::queue<DeferredPacket> pending;
        xSemaphoreTake(recvMutex_, portMAX_DELAY);
        std::swap(pending, recvQueue_);
        xSemaphoreGive(recvMutex_);

        while (!pending.empty()) {
            auto& pkt = pending.front();
            PacketCallback cb = m_pktHandlerCallbacks[(int)pkt.type].first;
            if (cb) {
                cb(pkt.srcMac, pkt.data.data(), pkt.data.size(),
                   m_pktHandlerCallbacks[(int)pkt.type].second);
            }
            pending.pop();
        }
    }

    void connect() override {
        // ESP-NOW requires station mode.
        WiFi.mode(WIFI_STA);

        // Stop the STA auto-reconnect scan loop: its periodic all-channel scans
        // pull the radio off ESPNOW_CHANNEL, so peers miss each other's unicast
        // ACKs. It keeps scanning even after a failed connect unless stopped.
        WiFi.setAutoReconnect(false);

        // Drop any AP but keep the radio on (false); ESP-NOW needs it active.
        WiFi.disconnect(false);

        // STA modem sleep defaults ON; with no AP the radio mostly sleeps and
        // peers can't return L2 ACKs, killing the link. PS_NONE keeps it on.
        esp_wifi_set_ps(WIFI_PS_NONE);

        // Small delay to let WiFi stabilize after mode change
        delay(100);

        channelPinPending_ = !pinEspNowChannel();
        if (channelPinPending_) {
            // A STA connect/scan can still be in flight here (the core can
            // schedule one last reconnect that lands after our disconnect),
            // and set_channel fails until it resolves. Without the retry,
            // ESP-NOW runs on whatever channel the scan stopped at until the
            // next mode switch.
            LOG_E("ENC", "channel pin to %d failed; retrying from exec()",
                  ESPNOW_CHANNEL);
            lastChannelPinMs_ = millis();
        }

        initializeEspNow();
        peerCommsState = PeerCommsState::CONNECTED;
    }

    void disconnect() override {
        // An excursion owns the radio now; the next connect() re-evaluates.
        channelPinPending_ = false;
        // esp_now_deinit() destroys the TX-complete callback the send queue
        // drains on, so a send in flight here orphans the queue forever. Clear it
        // first; the reliable layer resends anything that mattered after resume.
        clearSendQueue();

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
        
        // alloca the per-cluster buffer pointers: tiny, stack-cheap, and avoids a
        // mid-cluster malloc failure leaving a partially-built cluster.
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

        xSemaphoreTake(sendMutex_, portMAX_DELAY);
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
        xSemaphoreGive(sendMutex_);

        if(willNeedToStartSend)
        {
            SendFrontPkt();
        }
        return 0;
    }

    // One handler per packet type; registering replaces any existing one. ctx is
    // stored per type and passed back unmodified to the handler.
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

    int GetRssiForPeer(const uint8_t* macAddr) {
        uint64_t macAddr64 = MacToUInt64(macAddr);
        if(m_rssiTracker.count(macAddr64) > 0)
            return m_rssiTracker[macAddr64];
        return -1;
    }

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
    static EspNowDriver* instance;

    // Struct definitions must come before methods that use them
    struct DeferredPacket {
        PktType type;
        uint8_t srcMac[6];
        std::vector<uint8_t> data;
    };

    struct DataSendBuffer
    {
        uint8_t dstMac[6];
        uint8_t* ptr;
        size_t len;
    };

    struct DataRecvBuffer
    {
        uint8_t* data;
        unsigned long mostRecentRecvPktTime;
        uint8_t expectedNextIdx;
    };

    explicit EspNowDriver(const std::string& name) :
        PeerCommsDriverInterface(name),
        m_pktHandlerCallbacks((int)PktType::kNumPacketTypes, std::pair<PacketCallback, void*>(nullptr, nullptr)),
        m_maxRetries(5),
        m_curRetries(0),
        recvMutex_(xSemaphoreCreateMutex()),
        sendMutex_(xSemaphoreCreateMutex())
    {

#if PDN_ENABLE_RSSI_TRACKING
        // Gated: promiscuous mode is only needed for RSSI (the normal recv
        // callback doesn't expose it). Leaving it on unconditionally delivers
        // frames our STA MAC didn't filter, breaking the addressed-only unicast
        // assumption, hardware-confirmed to make uncabled devices latch each
        // other as wireless peers. Do not enable without RSSI tracking.
        wifi_promiscuous_filter_t filter = {
            .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
        esp_wifi_set_promiscuous_filter(&filter);
        esp_wifi_set_promiscuous_rx_cb(EspNowDriver::WifiPromiscuousRecvCallback);
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
        err = esp_now_register_recv_cb(EspNowDriver::EspNowRecvCallback);
        if(err != ESP_OK) {
            LOG_E("ENC", "ESPNOW Error registering recv cb: 0x%X\n", err);
            return -1;
        }
        
        err = esp_now_register_send_cb(EspNowDriver::EspNowSendCallback);
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

        EspNowDriver::GetInstance()->m_rssiTracker[srcMac64] = rssi;
    }

    //ESP-NOW callbacks
    static void EspNowRecvCallback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
        EspNowDriver* manager = EspNowDriver::GetInstance();

#if DEBUG_PRINT_ESP_NOW
        ESP_LOGD("ENC", "ESPNOW Recv Callback len %i from %X:%X:%X:%X:%X:%X\n", data_len,
            esp_now_info->src_addr[0], esp_now_info->src_addr[1], esp_now_info->src_addr[2],
            esp_now_info->src_addr[3], esp_now_info->src_addr[4], esp_now_info->src_addr[5]);
#endif

        if(data_len < sizeof(DataPktHdr)) {
            LOG_E("ENC", "Recieved buffer (%i bytes) was smaller than header (%u)\n", data_len, sizeof(DataPktHdr));
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
        EspNowDriver* manager = EspNowDriver::GetInstance();

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

        //TODO: Catch error and do reporting and push to next pkt
        manager->SendFrontPkt();
    }

    //Attempt to send the next packet in send queue
    int SendFrontPkt() {
        xSemaphoreTake(sendMutex_, portMAX_DELAY);
        if(m_sendQueue.empty()) {
            xSemaphoreGive(sendMutex_);
            return 0;
        }
        auto buffer = m_sendQueue.front();
        xSemaphoreGive(sendMutex_);

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
        xSemaphoreTake(sendMutex_, portMAX_DELAY);
        if (!m_sendQueue.empty()) {
            free(m_sendQueue.front().ptr);
            m_sendQueue.pop();
        }
        xSemaphoreGive(sendMutex_);
        m_curRetries = 0;
    }

    void clearSendQueue() {
        xSemaphoreTake(sendMutex_, portMAX_DELAY);
        while (!m_sendQueue.empty()) {
            free(m_sendQueue.front().ptr);
            m_sendQueue.pop();
        }
        xSemaphoreGive(sendMutex_);
        m_curRetries = 0;
    }

    int EnsurePeerIsRegistered(const uint8_t* mac_addr) {
        if(esp_now_is_peer_exist(mac_addr))
            return 0;

        esp_now_peer_num_t num_peers;
        esp_now_get_peer_num(&num_peers);
        if(num_peers.total_num > 19)
        {
            LOG_W("ENC", "ESP-NOW peer table full (20/20); cannot add new peer. "
                          "RDC should have evicted unused peers first.");
            return -1;
        }

        esp_now_peer_info_t new_peer = {};
        memcpy(new_peer.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
        new_peer.channel = 0;
        new_peer.ifidx = WIFI_IF_STA;
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

    SemaphoreHandle_t recvMutex_;
    std::queue<DeferredPacket> recvQueue_;

    SemaphoreHandle_t sendMutex_;

    //Storage for packet handler callbacks and their user args
    std::vector<std::pair<PacketCallback, void*>> m_pktHandlerCallbacks;

    void HandlePktCallback(const PktType packetType, const uint8_t* srcMacAddr, const uint8_t* pktData, const size_t pktLen) {
        if((int)packetType >= (int)PktType::kNumPacketTypes)
        {
            LOG_E("ENC", "Recv invalid packet type: %u\n", (int)packetType);
            return;
        }

        DeferredPacket pkt;
        pkt.type = packetType;
        memcpy(pkt.srcMac, srcMacAddr, 6);
        pkt.data.assign(pktData, pktData + pktLen);

        xSemaphoreTake(recvMutex_, portMAX_DELAY);
        recvQueue_.push(std::move(pkt));
        xSemaphoreGive(recvMutex_);
    }

    uint8_t* getMacAddress() override {
        esp_read_mac(macAddress_, ESP_MAC_WIFI_STA);
        return macAddress_;
    }

    const uint8_t* getGlobalBroadcastAddress() override {
        return PEER_BROADCAST_ADDR;
    }


    int addEspNowPeer(const uint8_t* macAddr) override {
        return EnsurePeerIsRegistered(macAddr);
    }

    int removeEspNowPeer(const uint8_t* macAddr) override {
        esp_err_t err = esp_now_del_peer(macAddr);
        if (err != ESP_OK && err != ESP_ERR_ESPNOW_NOT_FOUND) {
            LOG_W("ENC", "Failed to remove peer: 0x%X", err);
            return -1;
        }
        return 0;
    }

    // True when the radio is verifiably on ESPNOW_CHANNEL. esp_wifi_set_channel
    // ESP_FAILs while a STA connect/scan is in flight, so trust the readback,
    // not the call's return code.
    bool pinEspNowChannel() {
        esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
        uint8_t primary;
        wifi_second_chan_t secondary;
        esp_wifi_get_channel(&primary, &secondary);
        LOG_I("ENC", "WiFi channel set to: %d (requested: %d)", primary,
              ESPNOW_CHANNEL);
        return primary == ESPNOW_CHANNEL;
    }

    static constexpr unsigned long kChannelPinRetryMs = 100;
    bool channelPinPending_ = false;
    unsigned long lastChannelPinMs_ = 0;

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

    //Storage for rssi, which is captured by wifi promiscuous callback
    std::unordered_map<uint64_t, int> m_rssiTracker;
};
