#pragma once

#include <vector>
#include <queue>
#include <unordered_map>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "device/drivers/logger.hpp"
#include "device/drivers/driver-interface.hpp"
#include "wireless/mac-functions.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "esp32-driver-constants.hpp"

// sendData allocates every outbound packet with MALLOC_CAP_SPIRAM, so a build
// without PSRAM support fails here rather than at every send. A board whose PSRAM
// fails at boot still boots (SPIRAM_IGNORE_NOTFOUND), and then every send fails
// its allocation.
#if !defined(CONFIG_SPIRAM)
#error "esp-now-driver allocates with MALLOC_CAP_SPIRAM; CONFIG_SPIRAM must be enabled"
#endif

#define DEBUG_PRINT_ESP_NOW 0

//Change to 1 to enable tracking rssi for peers
//This works, but requires wifi to be in promiscuous mode
//which likely prevents connecting to access points and
//requires an unknown but likely high amount of processing
//power
#define PDN_ENABLE_RSSI_TRACKING 0

//Use this mac address in order to reach all nearby devices
constexpr uint8_t PEER_BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

constexpr size_t MAX_PKT_DATA_SIZE = ESP_NOW_MAX_DATA_LEN_V2 - sizeof(DataPktHdr);
static_assert(sizeof(FirmwareChunkHeader) + FIRMWARE_CHUNK_SIZE <= MAX_PKT_DATA_SIZE,
              "a firmware chunk must fit one ESP-NOW v2 frame");

//Singleton class that handles communication over ESP-NOW protocol.
class EspNowDriver : public PeerCommsDriverInterface
{
public:
    static EspNowDriver* CreateEspNowManager(const std::string& name) {
        instance = new EspNowDriver(name);
        return instance;
    }

    static EspNowDriver* GetInstance() {
        return instance;
    }

    // === PEER COMMS INTERFACE === //

    void exec() override {
        std::queue<DeferredPacket> pending;
        xSemaphoreTake(recvMutex, portMAX_DELAY);
        std::swap(pending, recvQueue_);
        recvQueueBytes = 0;
        const uint32_t dropped = droppedRecvPackets;
        droppedRecvPackets = 0;
        xSemaphoreGive(recvMutex);
        if (dropped > 0) {
            LOG_W("ENC", "Dropped %lu received packets: receive queue over %u bytes",
                  static_cast<unsigned long>(dropped), static_cast<unsigned>(RECV_QUEUE_MAX_BYTES));
        }

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
        // The STA netif must exist before esp_wifi_start(): esp_netif_start() runs
        // only off WIFI_EVENT_STA_START, so a netif created after that event has
        // been posted never starts, never gets an MTU, and DHCP refuses it.
        // ESP-NOW needs no netif; the HTTP driver's WiFi excursion does. This
        // runs first on every boot, but connect() re-runs on each return to
        // ESP-NOW mode, so the steps are guarded rather than assumed-once.
        wifi_mode_t currentMode;
        if (esp_wifi_get_mode(&currentMode) == ESP_ERR_WIFI_NOT_INIT) {
            // Both are checked because esp_netif_create_default_wifi_sta below
            // aborts through ESP_ERROR_CHECK rather than returning, so a failure
            // here would panic on the boot path instead of leaving a dead radio.
            // Already-created is the normal case when something else got here
            // first, and is not an error.
            const esp_err_t netifErr = esp_netif_init();
            const esp_err_t loopErr = esp_event_loop_create_default();
            if (netifErr != ESP_OK && netifErr != ESP_ERR_INVALID_STATE) {
                LOG_E("ENC", "esp_netif_init failed: %s", esp_err_to_name(netifErr));
                return;
            }
            if (loopErr != ESP_OK && loopErr != ESP_ERR_INVALID_STATE) {
                LOG_E("ENC", "esp_event_loop_create_default failed: %s", esp_err_to_name(loopErr));
                return;
            }
            wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
            esp_err_t initErr = esp_wifi_init(&config);
            if (initErr != ESP_OK) {
                LOG_E("ENC", "esp_wifi_init failed: %s", esp_err_to_name(initErr));
                return;
            }
            // RAM rather than flash, so radio config is not rewritten to NVS on
            // every change.
            esp_wifi_set_storage(WIFI_STORAGE_RAM);
        }
        esp_wifi_set_mode(WIFI_MODE_STA);
        // Same ifkey guard the HTTP driver uses, so whichever arrives first
        // creates it once. Creating it also installs IDF's default STA event
        // handlers, which is what makes esp_netif_start() fire below.
        if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
            esp_netif_create_default_wifi_sta();
        }
        esp_wifi_start();

        // Drops the AP association without stopping the radio, which ESP-NOW needs
        // left running.
        esp_wifi_disconnect();

        // Settle between the mode change and the channel pin: esp_wifi_set_channel
        // fails while a connect or scan is in flight.
        static constexpr uint32_t WIFI_SETTLE_MS = 100;
        vTaskDelay(pdMS_TO_TICKS(WIFI_SETTLE_MS));
        
        // Set the channel using ESP-IDF API for reliability
        esp_err_t err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
            LOG_E("ENC", "Failed to set channel %d: %s", ESPNOW_CHANNEL, esp_err_to_name(err));
        }

        uint8_t primary_channel;
        wifi_second_chan_t secondary_channel;
        esp_wifi_get_channel(&primary_channel, &secondary_channel);
        if (primary_channel != ESPNOW_CHANNEL) {
            LOG_E("ENC", "Channel pin did not take: on %d, wanted %d", primary_channel, ESPNOW_CHANNEL);
        } else {
            LOG_I("ENC", "WiFi channel set to: %d", primary_channel);
        }

        initializeEspNow();
        peerCommsState = PeerCommsState::CONNECTED;
    }

    void disconnect() override {
        esp_err_t err = esp_now_deinit();
        if(err != ESP_OK) {
            LOG_E("ENC", "ESPNOW Error deinitializing: 0x%X\n", err);
            return;
        }

        // The deinit took the IDF callback path with it, so no queued frame will
        // ever report; a stale head would stop sendData from ever starting
        // another transmission this boot.
        clearSendQueue();

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
        if (length > MAX_PKT_DATA_SIZE) {
            LOG_W("ENC", "ESP-NOW: Tried to send too large of buffer: %u of max %u\n",
                  length,
                  MAX_PKT_DATA_SIZE);
            return -1;
        }

        auto* sendBuffer = static_cast<uint8_t*>(
            heap_caps_malloc(sizeof(DataPktHdr) + length, MALLOC_CAP_SPIRAM));
        if (!sendBuffer) {
            // TODO: Return better error code once we have them
            LOG_E("ENC", "Failed to allocate buffer for ESP-NOW send queue");
            LOG_E("ENC", "Needed to allocate a total of %lu bytes\n", length);
            return -1;
        }

        DataPktHdr* hdr = reinterpret_cast<DataPktHdr*>(sendBuffer);
        hdr->packetType = packetType;

        memcpy(sendBuffer + sizeof(DataPktHdr), data, length);

        xSemaphoreTake(sendMutex, portMAX_DELAY);
        bool willNeedToStartSend = m_sendQueue.empty();

        DataSendBuffer buffer;
        memcpy(buffer.dstMac, dst, ESP_NOW_ETH_ALEN);
        buffer.ptr = sendBuffer;
        buffer.len = sizeof(DataPktHdr) + length;
        m_sendQueue.push(buffer);

        xSemaphoreGive(sendMutex);

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

    /** True when no frame is queued or awaiting its send report. */
    bool isSendQueueEmpty() override {
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        const bool empty = m_sendQueue.empty();
        xSemaphoreGive(sendMutex);
        return empty;
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

    int getRssiForPeer(const uint8_t* macAddr) override {
        return GetRssiForPeer(macAddr);
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

    explicit EspNowDriver(const std::string& name)
        : PeerCommsDriverInterface(name)
        , recvMutex(xSemaphoreCreateMutex())
        , sendMutex(xSemaphoreCreateMutex())
        , m_pktHandlerCallbacks((int)PktType::kNumPacketTypes, std::pair<PacketCallback, void*>(nullptr, nullptr))
        , m_maxRetries(5)
        , m_curRetries(0) {

        wifi_promiscuous_filter_t filter = {
            .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
        esp_wifi_set_promiscuous_filter(&filter);
        esp_wifi_set_promiscuous_rx_cb(EspNowDriver::WifiPromiscuousRecvCallback);
        esp_wifi_set_promiscuous(true);
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

        manager->HandlePktCallback(pktHdr->packetType, esp_now_info->src_addr,
                                   data + sizeof(DataPktHdr), data_len - sizeof(DataPktHdr));
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

        manager->releaseTransmissionClaim();
        manager->SendFrontPkt();
    }

    //Attempt to send the next packet in send queue
    int SendFrontPkt() {
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        if (m_sendQueue.empty() || transmissionClaimed) {
            xSemaphoreGive(sendMutex);
            return 0;
        }
        transmissionClaimed = true;
        DataSendBuffer buffer = m_sendQueue.front();
        xSemaphoreGive(sendMutex);

        if (memcmp(buffer.dstMac, PEER_BROADCAST_ADDR, ESP_NOW_ETH_ALEN) != 0)
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
                    MoveToNextSendPkt();
                    releaseTransmissionClaim();
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
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        if (!m_sendQueue.empty()) {
            free(m_sendQueue.front().ptr);
            m_sendQueue.pop();
        }
        xSemaphoreGive(sendMutex);
        m_curRetries = 0;
    }

    void releaseTransmissionClaim() {
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        transmissionClaimed = false;
        xSemaphoreGive(sendMutex);
    }

    void clearSendQueue() {
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        while (!m_sendQueue.empty()) {
            free(m_sendQueue.front().ptr);
            m_sendQueue.pop();
        }
        transmissionClaimed = false;
        xSemaphoreGive(sendMutex);
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

    SemaphoreHandle_t recvMutex;
    std::queue<DeferredPacket> recvQueue_;
    // Payload bytes queued since the last exec(). Each buffer is under
    // SPIRAM_MALLOC_ALWAYSINTERNAL, so it comes from internal RAM, and a seed's
    // chunks arriving through setup's splash hold, before the first exec(), would
    // exhaust it; a packet over the budget is dropped like one lost on the air.
    size_t recvQueueBytes = 0;
    uint32_t droppedRecvPackets = 0;
    static constexpr size_t RECV_QUEUE_MAX_BYTES = 16384;

    SemaphoreHandle_t sendMutex;

    //Storage for packet handler callbacks and their user args
    std::vector<std::pair<PacketCallback, void*>> m_pktHandlerCallbacks;

    void HandlePktCallback(const PktType packetType, const uint8_t* srcMacAddr, const uint8_t* pktData, const size_t pktLen) {
        if((int)packetType >= (int)PktType::kNumPacketTypes)
        {
            LOG_E("ENC", "Recv invalid packet type: %u\n", (int)packetType);
            return;
        }

        xSemaphoreTake(recvMutex, portMAX_DELAY);
        if (recvQueueBytes + pktLen > RECV_QUEUE_MAX_BYTES) {
            droppedRecvPackets++;
            xSemaphoreGive(recvMutex);
            return;
        }
        recvQueueBytes += pktLen;
        xSemaphoreGive(recvMutex);

        DeferredPacket pkt;
        pkt.type = packetType;
        memcpy(pkt.srcMac, srcMacAddr, 6);
        pkt.data.assign(pktData, pktData + pktLen);

        xSemaphoreTake(recvMutex, portMAX_DELAY);
        recvQueue_.push(std::move(pkt));
        xSemaphoreGive(recvMutex);
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

    PeerCommsState peerCommsState = PeerCommsState::DISCONNECTED;

    //Storage for MAC address
    uint8_t macAddress_[6];

    //Storage for retry handling
    uint8_t m_maxRetries;
    uint8_t m_curRetries;

    //Packet send queue
    std::queue<DataSendBuffer> m_sendQueue;

    // Set while a frame has been handed to esp_now_send and its completion has not
    // been reported yet. Guarded by sendMutex: the main loop and the WiFi task both
    // reach SendFrontPkt, and without the claim both can transmit the same entry.
    bool transmissionClaimed = false;

    //Storage for rssi, which is captured by wifi promiscuous callback
    std::unordered_map<uint64_t, int> m_rssiTracker;
};
