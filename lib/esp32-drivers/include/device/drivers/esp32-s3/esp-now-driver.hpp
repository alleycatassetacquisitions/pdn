#pragma once

#include <algorithm>
#include <vector>
#include <queue>
#include <unordered_map>
#include <limits>
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

//Use this mac address in order to reach all nearby devices
constexpr uint8_t PEER_BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Max application payload per frame. ESP-NOW v2.0 (IDF 5.x, all-S3 targets)
// carries up to 1470 bytes in a single frame, so every packet type fits in
// one send — no multi-frame clustering.
// The payload ceiling is whatever DataPktHdr::pktLen can hold (it stores the
// total packet length), minus the header. ESP-NOW v2 frames are far larger,
// but the length field is the real ceiling: a payload above this would overflow
// pktLen and deliver a corrupt short buffer. Derived from the field's type so
// it follows automatically if pktLen ever widens.
constexpr size_t MAX_PKT_DATA_SIZE =
    std::numeric_limits<decltype(DataPktHdr::pktLen)>::max() - sizeof(DataPktHdr);

//Singleton class that handles communication over ESP-NOW protocol.
class EspNowManager : public PeerCommsDriverInterface
{
public:
    /// Creates the process-wide singleton; call once at driver registration.
    static EspNowManager* CreateEspNowManager(const std::string& name) {
        instance = new EspNowManager(name);
        return instance;
    }

    /// The process-wide singleton, or nullptr before CreateEspNowManager.
    static EspNowManager* GetInstance() {
        return instance;
    }

    // === PEER COMMS INTERFACE === //

    /// Main-loop tick: drains the deferred receive queue into the per-type
    /// packet handlers, and retries a pending channel pin.
    void exec() override {
        // Re-pin armed by connect(): retry (paced) until the readback sticks,
        // re-issuing the disconnect each attempt to kill whatever STA attempt
        // is blocking the pin. Stops if an excursion takes the radio.
        if (channelPinPending && peerCommsState == PeerCommsState::CONNECTED) {
            unsigned long now = millis();
            if (now - lastChannelPinMs >= CHANNEL_PIN_RETRY_MS) {
                lastChannelPinMs = now;
                WiFi.disconnect(false);
                if (pinEspNowChannel()) {
                    channelPinPending = false;
                    LOG_W("ENC", "channel pin recovered: on %d", ESPNOW_CHANNEL);
                }
            }
        }

        std::queue<DeferredPacket> pending;
        xSemaphoreTake(recvMutex, portMAX_DELAY);
        std::swap(pending, recvQueue);
        xSemaphoreGive(recvMutex);

        while (!pending.empty()) {
            auto& pkt = pending.front();
            PacketCallback cb = pktHandlerCallbacks[(int)pkt.type].first;
            if (cb) {
                cb(pkt.srcMac, pkt.data.data(), pkt.data.size(),
                   pktHandlerCallbacks[(int)pkt.type].second);
            }
            pending.pop();
        }

        std::queue<DeferredSendResult> sendResults;
        xSemaphoreTake(sendResultMutex, portMAX_DELAY);
        std::swap(sendResults, sendResultQueue);
        xSemaphoreGive(sendResultMutex);

        while (!sendResults.empty()) {
            auto& r = sendResults.front();
            SendStatusCallback cb = sendStatusHandlers_[(int)r.type].first;
            if (cb) {
                cb(r.dstMac, r.data.data(), r.data.size(), r.success,
                   sendStatusHandlers_[(int)r.type].second);
            }
            sendResults.pop();
        }
    }

    /// Brings the radio into ESP-NOW mode: STA, auto-reconnect off, PS_NONE,
    /// channel pinned (with exec() retry when a STA scan blocks the pin).
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

        channelPinPending = !pinEspNowChannel();
        if (channelPinPending) {
            // A STA connect/scan can still be in flight here (the core can
            // schedule one last reconnect that lands after our disconnect),
            // and set_channel fails until it resolves. Without the retry,
            // ESP-NOW runs on whatever channel the scan stopped at until the
            // next mode switch.
            LOG_E("ENC", "channel pin to %d failed; retrying from exec()",
                  ESPNOW_CHANNEL);
            lastChannelPinMs = millis();
        }

        initializeEspNow();
        peerCommsState = PeerCommsState::CONNECTED;
    }

    /// Tears down ESP-NOW for a WiFi excursion; clears the send queue first
    /// (an in-flight send orphaned across deinit stalls TX forever).
    void disconnect() override {
        // An excursion owns the radio now; the next connect() re-evaluates.
        channelPinPending = false;
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

    /// Current radio mode (ESP-NOW connected vs released for WiFi).
    PeerCommsState getPeerCommsState() override {
        return peerCommsState;
    }

    /// Transitions the radio between ESP-NOW and released states.
    void setPeerCommsState(PeerCommsState state) override {
        if(state == PeerCommsState::CONNECTED && peerCommsState != PeerCommsState::CONNECTED) {
            connect();
        }
        else if(state == PeerCommsState::DISCONNECTED && peerCommsState != PeerCommsState::DISCONNECTED) {
            disconnect();
        }
    }

    // Queues up data for sending, may not send right away
    /// Queues data for sending; may not send right away.
    int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) override {
        if (length > MAX_PKT_DATA_SIZE) {
            LOG_W("ENC", "ESP-NOW: Tried to send too large of buffer: %u of max %u\n",
                  length,
                  MAX_PKT_DATA_SIZE);
            return -1;
        }

        // One frame carries the whole payload (ESP-NOW v2.0). Build header +
        // data in a single buffer and queue it.
        uint8_t* buf = (uint8_t*)ps_malloc(sizeof(DataPktHdr) + length);
        if (!buf) {
            LOG_E("ENC", "Failed to allocate %lu byte buffer for ESP-NOW send\n",
                  sizeof(DataPktHdr) + length);
            return -1;
        }

        DataPktHdr* hdr = reinterpret_cast<DataPktHdr*>(buf);
        hdr->pktLen = sizeof(DataPktHdr) + length;
        hdr->packetType = packetType;
        memcpy(buf + sizeof(DataPktHdr), data, length);

        DataSendBuffer buffer;
        memcpy(buffer.dstMac, dst, ESP_NOW_ETH_ALEN);
        buffer.ptr = buf;
        buffer.len = hdr->pktLen;

        xSemaphoreTake(sendMutex, portMAX_DELAY);
        bool willNeedToStartSend = sendQueue.empty();
        sendQueue.push(buffer);
        xSemaphoreGive(sendMutex);

        if(willNeedToStartSend)
        {
            SendFrontPkt();
        }
        return 0;
    }

    // Set the packet handler for a particular packet type
    // Only one handler can be registered per packet type at a time, so if a new
    // packet handler is registered for a packet type that has an existing handler,
    // the existing handler is automatically unregistered
    // userArg will be saved per packet type and will be passed in unmodified to
    // packet handler for that packet type (when a packet of that type is receieved)
    /// One handler per packet type; registering replaces any existing one.
    /// ctx is stored per type and passed back unmodified to the handler.
    void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) override {
        pktHandlerCallbacks[(int)packetType].first = callback;
        pktHandlerCallbacks[(int)packetType].second = ctx;
    }

    // Unregister packet handler for specified packet type
    /// Removes the handler for a packet type.
    void clearPacketHandler(PktType packetType) override {
        pktHandlerCallbacks[(int)packetType].first = nullptr;
    }

    /// Radio send-result handler, one per PktType. Fired from exec() (deferred
    /// to the main loop) when the send callback reports SEND_SUCCESS or a final
    /// SEND_FAIL for a packet of this type. Drives the reliable-transport ack.
    void setSendStatusHandler(PktType packetType, SendStatusCallback callback, void* ctx) override {
        sendStatusHandlers_[(int)packetType].first = callback;
        sendStatusHandlers_[(int)packetType].second = ctx;
    }

    /// Removes the send-result handler for a packet type.
    void clearSendStatusHandler(PktType packetType) override {
        sendStatusHandlers_[(int)packetType].first = nullptr;
    }

    // Called by DriverManager at startup - we don't initialize ESP-NOW here
    // because WiFi must be set up first. Actual init happens in connect().
    /// Driver-interface initialization hook; radio setup happens in connect().
    int initialize() override {
        // No-op: ESP-NOW initialization requires WiFi to be running first.
        // The actual initialization happens in connect() -> initializeEspNow()
        return 0;
    }

    /// Last RSSI captured for a peer from its receive callbacks; -1 if none seen.
    int GetRssiForPeer(const uint8_t* macAddr) {
        uint64_t macAddr64 = MacToUInt64(macAddr);
        if (rssiTracker.count(macAddr64) > 0)
            return rssiTracker[macAddr64];
        return -1;
    }

    /// PeerCommsInterface adapter for GetRssiForPeer.
    int getRssiForPeer(const uint8_t* macAddr) override {
        return GetRssiForPeer(macAddr);
    }

    // Public methods for ESP-NOW callback handling
    // (used when re-initializing ESP-NOW in EspNowState)
    /// Forwards to the static ESP-NOW receive callback (used when
    /// re-initializing ESP-NOW).
    void HandleReceivedData(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
        // This simply forwards to the static callback method
        EspNowRecvCallback(esp_now_info, data, data_len);
    }

    /// Forwards to the static ESP-NOW send-status callback.
    void HandleSendStatus(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status) {
        // This simply forwards to the static callback method
        EspNowSendCallback(esp_now_info, status);
    }

private:
    static EspNowManager* instance;

    // Struct definitions must come before methods that use them
    struct DeferredPacket {
        PktType type;
        uint8_t srcMac[6];
        std::vector<uint8_t> data;
    };

    // A send callback result queued off the WiFi-task callback for the main
    // loop to dispatch. `data` is the payload (past the DataPktHdr) of the
    // packet that was sent, so the reliable channel can read its seqId back out.
    struct DeferredSendResult {
        PktType type;
        uint8_t dstMac[6];
        std::vector<uint8_t> data;
        bool success;
    };

    struct DataSendBuffer
    {
        uint8_t dstMac[6];
        uint8_t* ptr;
        size_t len;
    };

    explicit EspNowManager(const std::string& name)
        : PeerCommsDriverInterface(name)
        , pktHandlerCallbacks((int)PktType::kNumPacketTypes, std::pair<PacketCallback, void*>(nullptr, nullptr))
        , sendStatusHandlers_((int)PktType::kNumPacketTypes, std::pair<SendStatusCallback, void*>(nullptr, nullptr))
        , maxRetries(5)
        , curRetries(0)
        , recvMutex(xSemaphoreCreateMutex())
        , sendMutex(xSemaphoreCreateMutex()) {
        sendResultMutex = xSemaphoreCreateMutex();
        // ESP-NOW initialization happens in connect() -> initializeEspNow()
        // after WiFi has been set up. RSSI is read directly from each receive
        // callback (esp_now_recv_info_t::rx_ctrl), so no promiscuous mode.
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

    //ESP-NOW callbacks
    static void EspNowRecvCallback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
        EspNowManager* manager = EspNowManager::GetInstance();

        // rx_ctrl carries the per-frame RSSI, available on every ESP-NOW
        // receive without promiscuous mode. This is the sole RSSI fill path.
        if (esp_now_info->rx_ctrl != nullptr) {
            uint64_t srcMac64 = MacToUInt64(esp_now_info->src_addr);
            manager->rssiTracker[srcMac64] = esp_now_info->rx_ctrl->rssi;
        }

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

        manager->handleSinglePacket(esp_now_info->src_addr, data, pktHdr);
    }

    // Helper methods for packet reception
    void handleSinglePacket(const uint8_t* mac_addr, const uint8_t* data, const DataPktHdr* pktHdr) {
        HandlePktCallback(pktHdr->packetType, mac_addr, data + sizeof(DataPktHdr), pktHdr->pktLen - sizeof(DataPktHdr));
    }

    static void EspNowSendCallback(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status) {
        EspNowManager* manager = EspNowManager::GetInstance();

#if DEBUG_PRINT_ESP_NOW
        ESP_LOGD("ENC", "ESPNOW Send Callback");
#endif

        // tx_info->tx_status is the forward-compatible source; the `status`
        // parameter is documented for removal in a future IDF release. The two
        // enums share values (ESP_NOW_SEND_SUCCESS == WIFI_SEND_SUCCESS).
        if (esp_now_info->tx_status == WIFI_SEND_SUCCESS) {
            LOG_D("ENC", "Send SUCCESS");
            manager->deferSendResult(true);
            manager->MoveToNextSendPkt();
        } else {
            if (manager->curRetries < manager->maxRetries) {
                LOG_W("ENC", "Send FAILED (retry %d/%d)",
                      manager->curRetries + 1, manager->maxRetries);
                ++manager->curRetries;
            } else {
                LOG_E("ENC", "Send FAILED - giving up after %d retries",
                      manager->maxRetries);
                manager->deferSendResult(false);
                manager->MoveToNextSendPkt();
            }
        }

        //TODO: Catch error and do reporting and push to next pkt
        manager->SendFrontPkt();
    }

    //Attempt to send the next packet in send queue
    int SendFrontPkt() {
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        if (sendQueue.empty()) {
            xSemaphoreGive(sendMutex);
            return 0;
        }
        DataSendBuffer buffer = sendQueue.front();
        xSemaphoreGive(sendMutex);

        // Make sure a unicast peer is registered before sending to it.
        if (memcmp(buffer.dstMac, PEER_BROADCAST_ADDR, ESP_NOW_ETH_ALEN) != 0)
            EnsurePeerIsRegistered(buffer.dstMac);

        esp_err_t err;
        do
        {
            err = esp_now_send(buffer.dstMac, buffer.ptr, buffer.len);
            if(err != ESP_OK)
            {
                ++curRetries;
                if (curRetries >= maxRetries) {
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
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        if (!sendQueue.empty()) {
            free(sendQueue.front().ptr);
            sendQueue.pop();
        }
        xSemaphoreGive(sendMutex);
        curRetries = 0;
    }

    // Snapshot the just-finished send (still at sendQueue.front()) onto the
    // deferred queue for exec() to dispatch on the main loop. Call BEFORE
    // MoveToNextSendPkt, which frees the front buffer. Broadcast/unsequenced
    // sends are queued too; the reliable channel drops them (no seqId match).
    void deferSendResult(bool success) {
        DeferredSendResult res;
        bool have = false;
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        if (!sendQueue.empty()) {
            const DataSendBuffer& buf = sendQueue.front();
            const DataPktHdr* hdr = reinterpret_cast<const DataPktHdr*>(buf.ptr);
            res.type = hdr->packetType;
            memcpy(res.dstMac, buf.dstMac, 6);
            const uint8_t* payload = buf.ptr + sizeof(DataPktHdr);
            size_t payloadLen = buf.len > sizeof(DataPktHdr) ? buf.len - sizeof(DataPktHdr) : 0;
            res.data.assign(payload, payload + payloadLen);
            res.success = success;
            have = true;
        }
        xSemaphoreGive(sendMutex);
        if (!have) return;
        xSemaphoreTake(sendResultMutex, portMAX_DELAY);
        sendResultQueue.push(std::move(res));
        xSemaphoreGive(sendResultMutex);
    }

    void clearSendQueue() {
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        while (!sendQueue.empty()) {
            free(sendQueue.front().ptr);
            sendQueue.pop();
        }
        xSemaphoreGive(sendMutex);
        curRetries = 0;
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
    std::queue<DeferredPacket> recvQueue;

    SemaphoreHandle_t sendMutex;

    //Storage for packet handler callbacks and their user args
    std::vector<std::pair<PacketCallback, void*>> pktHandlerCallbacks;
    // Send-result handlers, one per PktType, mirroring pktHandlerCallbacks.
    std::vector<std::pair<SendStatusCallback, void*>> sendStatusHandlers_;

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

        xSemaphoreTake(recvMutex, portMAX_DELAY);
        recvQueue.push(std::move(pkt));
        xSemaphoreGive(recvMutex);
    }

    uint8_t* getMacAddress() override {
        esp_read_mac(macAddress, ESP_MAC_WIFI_STA);
        return macAddress;
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

    static constexpr unsigned long CHANNEL_PIN_RETRY_MS = 100;
    bool channelPinPending = false;
    unsigned long lastChannelPinMs = 0;

    PeerCommsState peerCommsState = PeerCommsState::DISCONNECTED;

    //Storage for MAC address
    uint8_t macAddress[6];

    //Storage for retry handling
    uint8_t maxRetries;
    uint8_t curRetries;

    //Packet send queue
    std::queue<DataSendBuffer> sendQueue;

    // Deferred send-result queue: the WiFi-task send callback snapshots each
    // finished send here; exec() drains it on the main loop so the reliable
    // transport ack never races Resender::sync().
    SemaphoreHandle_t sendResultMutex;
    std::queue<DeferredSendResult> sendResultQueue;

    // Storage for rssi, filled from each ESP-NOW receive callback (rx_ctrl)
    std::unordered_map<uint64_t, int> rssiTracker;
};
