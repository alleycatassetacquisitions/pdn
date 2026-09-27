#pragma once

#include <algorithm>
#include <atomic>
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
constexpr size_t MAX_PKT_DATA_SIZE = ESP_NOW_MAX_DATA_LEN_V2 - sizeof(DataPktHdr);

// pktLen carries the total packet length on the wire, so it has to be able to
// express a full frame or a large payload would arrive as a corrupt short buffer.
static_assert(ESP_NOW_MAX_DATA_LEN_V2 <=
                  std::numeric_limits<decltype(DataPktHdr::pktLen)>::max(),
              "DataPktHdr::pktLen cannot express a full ESP-NOW v2 frame");

// The head-roster handoff is the largest reliable payload and grows with
// MAX_CHAIN_MEMBERS. A cap bump that overflows this budget must break the build
// here, not silently fail sendData at runtime.
static_assert(sizeof(HeadTransferPayload) <= MAX_PKT_DATA_SIZE,
              "HeadTransferPayload exceeds the reliable-payload budget; "
              "lower MAX_CHAIN_MEMBERS or split the transfer across frames");

//Singleton class that handles communication over ESP-NOW protocol.
class EspNowDriver : public PeerCommsDriverInterface
{
public:
    /// Creates the process-wide singleton; call once at driver registration.
    static EspNowDriver* CreateEspNowManager(const std::string& name) {
        instance = new EspNowDriver(name);
        return instance;
    }

    /// The process-wide singleton, or nullptr before CreateEspNowManager.
    static EspNowDriver* GetInstance() {
        return instance;
    }

    // === PEER COMMS INTERFACE === //

    /// Main-loop tick: retries a pending channel pin, drains the deferred receive
    /// queue into the per-type packet handlers, then resolves any finished send
    /// and hands the radio the next frame.
    ///
    /// Claims at most one frame per call, so a backlog drains over as many
    /// Device::loop() iterations. sendData pumps its own frame, so this is the
    /// path for frames left queued behind a busy slot, not for every send.
    /// DriverManager walks a std::map keyed by driver name, and "peer_comms"
    /// sorts ahead of "serial_in"/"serial_out", so a frame queued by a
    /// serial-driven handler waits an extra tick.
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

        // Order matters; serviceSendCompletion says why.
        serviceSendCompletion();
        pumpSend();
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

    /// Tears down ESP-NOW for a WiFi excursion, releasing both the queued frames
    /// and the one the radio holds.
    void disconnect() override {
        // An excursion owns the radio now; the next connect() re-evaluates.
        channelPinPending = false;
        // Whatever is still queued does not go out: it would arrive after the
        // excursion as stale traffic, and the reliable layer resends what matters
        // once the radio is back. Nothing is handed to the radio here either —
        // esp_now_send returns before the frame is on the air, so the deinit below
        // would race it, and claiming a frame without first consuming a recorded
        // verdict would throw that verdict away.
        clearSendQueue();

        esp_err_t err = esp_now_deinit();
        if (err != ESP_OK) LOG_E("ENC", "ESPNOW Error deinitializing: 0x%X\n", err);

        // Unconditional: deinit unregisters the send callback, so the completion
        // that would clear the slot is never coming, and an occupied slot blocks
        // every later pumpSend. Leaving the state CONNECTED would stop connect()
        // re-running. A completion that does still arrive lands on an empty slot
        // and serviceSendCompletion drops it.
        discardInFlight();
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

    /// Queues the frame and offers it to the radio; a frame behind a busy slot
    /// waits for a later exec().
    int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) override {
        // Refused rather than queued while the radio is down. A queued frame
        // would outlive the excursion and go out afterwards as stale traffic,
        // and an excursion long enough to matter would grow the queue by one
        // allocation per attempt. Callers that care retry; the reliable layer
        // has its own timer.
        if (peerCommsState != PeerCommsState::CONNECTED) return -1;
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
        sendQueue.push(buffer);
        xSemaphoreGive(sendMutex);

        // Offered now rather than at the next exec(). Every caller is on the main
        // loop, so the slot keeps its single owner, and a frame queued before
        // Device::loop() starts still reaches the radio — the crash-log backlog is
        // queued from setup(), where no exec() runs.
        pumpSend();
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

    /// Radio send-result handler, one per PktType. Fired on the main loop, from
    /// whichever call resolves the frame, when the send callback reports
    /// SEND_SUCCESS or a final SEND_FAIL for a packet of this type. Drives the
    /// reliable-transport ack.
    void setSendStatusHandler(PktType packetType, SendStatusCallback callback, void* ctx) override {
        sendStatusHandlers[(int)packetType].first = callback;
        sendStatusHandlers[(int)packetType].second = ctx;
    }

    /// Removes the send-result handler for a packet type.
    void clearSendStatusHandler(PktType packetType) override {
        sendStatusHandlers[(int)packetType].first = nullptr;
    }

    // Called by DriverManager at startup - we don't initialize ESP-NOW here
    // because WiFi must be set up first. Actual init happens in connect().
    /// Driver-interface initialization hook; radio setup happens in connect().
    int initialize() override {
        // No-op: ESP-NOW initialization requires WiFi to be running first.
        // The actual initialization happens in connect() -> initializeEspNow()
        return 0;
    }

    /// Last RSSI captured for a peer from its receive callbacks; RSSI_UNKNOWN if
    /// none seen.
    int getRssiForPeer(const uint8_t* macAddr) override {
        const uint64_t macAddr64 = MacToUInt64(macAddr);
        int rssi = RSSI_UNKNOWN;
        xSemaphoreTake(recvMutex, portMAX_DELAY);
        std::unordered_map<uint64_t, int>::const_iterator entry =
            rssiTracker.find(macAddr64);
        if (entry != rssiTracker.end()) rssi = entry->second;
        xSemaphoreGive(recvMutex);
        return rssi;
    }

private:
    static EspNowDriver* instance;

    // What the radio's send callback recorded about the frame in the slot. The
    // callback carries no handle for the frame it finished, so this is a verdict
    // rather than an identity: exec() pairs it with whatever the slot holds.
    enum class Completion : uint8_t { NONE, SUCCEEDED, FAILED };

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
        , pktHandlerCallbacks((int)PktType::kNumPacketTypes, std::pair<PacketCallback, void*>(nullptr, nullptr))
        , sendStatusHandlers((int)PktType::kNumPacketTypes, std::pair<SendStatusCallback, void*>(nullptr, nullptr))
        , recvMutex(xSemaphoreCreateMutex())
        , sendMutex(xSemaphoreCreateMutex()) {
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

        // pktLen comes off the wire and is what bounds the copy handed to the packet
        // handlers, so it has to agree with what the radio actually delivered.
        // sendData is the sole writer and sets the two equal. Below the header size it
        // would also underflow handleSinglePacket's subtraction to about SIZE_MAX.
        if (pktHdr->pktLen < sizeof(DataPktHdr) ||
            static_cast<size_t>(pktHdr->pktLen) != static_cast<size_t>(data_len)) {
            LOG_E("ENC", "Declared pktLen %u disagrees with received %i from %X:%X:%X:%X:%X:%X\n",
                  pktHdr->pktLen, data_len,
                  esp_now_info->src_addr[0], esp_now_info->src_addr[1], esp_now_info->src_addr[2],
                  esp_now_info->src_addr[3], esp_now_info->src_addr[4], esp_now_info->src_addr[5]);
            return;
        }

#if DEBUG_PRINT_ESP_NOW
        ESP_LOGD("ENC", "Packet Type: %i\n", pktHdr->packetType);
#endif

        // Below the length checks, so a frame whose declared length disagrees with
        // what arrived cannot seed an entry. The packet type is validated later, in
        // HandlePktCallback, so a well-framed frame with a junk type still does.
        // Shares recvMutex: the main loop reads this through getRssiForPeer.
        if (esp_now_info->rx_ctrl != nullptr) {
            const uint64_t srcMac64 = MacToUInt64(esp_now_info->src_addr);
            xSemaphoreTake(manager->recvMutex, portMAX_DELAY);
            manager->rssiTracker[srcMac64] = esp_now_info->rx_ctrl->rssi;
            xSemaphoreGive(manager->recvMutex);
        }

        manager->handleSinglePacket(esp_now_info->src_addr, data, pktHdr);
    }

    // Helper methods for packet reception
    void handleSinglePacket(const uint8_t* mac_addr, const uint8_t* data, const DataPktHdr* pktHdr) {
        HandlePktCallback(pktHdr->packetType, mac_addr, data + sizeof(DataPktHdr), pktHdr->pktLen - sizeof(DataPktHdr));
    }

    static void EspNowSendCallback(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status) {
        EspNowDriver* manager = EspNowDriver::GetInstance();

#if DEBUG_PRINT_ESP_NOW
        ESP_LOGD("ENC", "ESPNOW Send Callback");
#endif

        // Records the verdict and returns; exec() acts on it against the slot on
        // the main loop. That split is what makes the slot single-writer: claim,
        // transmit, retry and release all happen on one task, so none can run
        // underneath another.
        //
        // tx_info->tx_status is the forward-compatible source; the `status`
        // parameter is documented for removal in a future IDF release. The two
        // enums share values (ESP_NOW_SEND_SUCCESS == WIFI_SEND_SUCCESS).
        manager->pendingCompletion.store(
            esp_now_info->tx_status == WIFI_SEND_SUCCESS ? Completion::SUCCEEDED
                                                        : Completion::FAILED,
            std::memory_order_release);
    }

    /// Resolves a recorded completion against the slot: retry, or report and
    /// release. Main loop only, from exec().
    ///
    /// A frame the radio accepts but never reports on holds the slot for good and
    /// every later send queues behind it. Do not answer that with a timer: the
    /// radio reports a peer, not a frame, so two frames to one peer are
    /// indistinguishable and a slot released on a deadline charges the late
    /// verdict to whichever frame replaced it, turning a stall into a wrong ack.
    void serviceSendCompletion() {
        const Completion outcome =
            pendingCompletion.exchange(Completion::NONE, std::memory_order_acquire);
        if (outcome == Completion::NONE) return;
        // Nothing in the slot means the radio owed this for a frame disconnect()
        // already released. There is no frame to attribute it to, so drop it.
        // Running before pumpSend is what makes that sufficient: a stale verdict
        // is always consumed against an empty slot before the next frame is
        // claimed, so it can never be charged to a successor.
        if (inFlight.ptr == nullptr) return;

        if (outcome == Completion::SUCCEEDED) {
            LOG_D("ENC", "Send SUCCESS");
            finishInFlight(true);
            return;
        }
        // Bounded by attempts, not wall clock. A retry is issued from exec(), so
        // each one already costs a loop period; a millisecond ceiling on top of
        // that would let the loop's own load decide how many attempts a frame
        // gets, and several senders have no retry but this one.
        if (inFlightRetries < MAX_SEND_RETRIES) {
            ++inFlightRetries;
            LOG_W("ENC", "Send FAILED (retry %d/%d)", inFlightRetries,
                  MAX_SEND_RETRIES);
            if (transmitInFlight()) return;
            // Refused, so no completion is coming for it either.
            finishInFlight(false);
            return;
        }
        LOG_E("ENC", "Send FAILED after %d attempts", inFlightRetries + 1);
        finishInFlight(false);
    }

    /// Hands the radio the next waiting frame, if it is not already holding one.
    /// Main loop only.
    ///
    /// The frame outlives the attempt for this driver's sake, not the radio's: the
    /// radio copies it before esp_now_send returns (esp_now.h attention 4). A retry
    /// re-sends the same bytes and the completion reads the payload back.
    void pumpSend() {
        // sendData refuses while the radio is down, so this covers the frame that
        // was queued while it was up and lost the race with disconnect().
        if (peerCommsState != PeerCommsState::CONNECTED) return;
        if (inFlight.ptr != nullptr) return;

        // The lock covers sendQueue and nothing else; the slot is this task's.
        DataSendBuffer next{};
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        if (sendQueue.empty()) {
            xSemaphoreGive(sendMutex);
            return;
        }
        next = sendQueue.front();
        sendQueue.pop();
        xSemaphoreGive(sendMutex);

        inFlight = next;
        inFlightRetries = 0;
        if (transmitInFlight()) return;

        // The radio never saw the air, so no completion is coming. One frame per
        // tick rather than draining here: ESP_ERR_ESPNOW_NO_MEM asks for a pause
        // before the next frame (esp_now.h), and a queue drained into a full
        // internal TX buffer fails every frame in it.
        finishInFlight(false);
    }

    /// Hands `inFlight` to the radio. True when the radio took it and a completion
    /// is owed. Main loop only.
    bool transmitInFlight() {
        if (memcmp(inFlight.dstMac, PEER_BROADCAST_ADDR, ESP_NOW_ETH_ALEN) != 0)
            EnsurePeerIsRegistered(inFlight.dstMac);

        const esp_err_t err = esp_now_send(inFlight.dstMac, inFlight.ptr, inFlight.len);
        if (err == ESP_OK) return true;
        LOG_E("ENC", "ESPNOW send refused: 0x%X", err);
        return false;
    }

    /// Reports the slot's frame upward and releases it. Main loop only.
    ///
    /// Reports the failure verdict even though nothing consumes it today:
    /// ReliableChannelBase is the only registrant and drops `!success` on its first
    /// line. An outcome class is the driver's to report, not to decide against.
    void finishInFlight(bool success) {
        const DataSendBuffer frame = inFlight;
        inFlight = {};
        if (frame.ptr == nullptr) return;

        // Emptied before dispatch so a handler that tears the radio down cannot
        // double-free this frame: discardInFlight would free the slot's pointer,
        // and the free below owns it.
        const DataPktHdr* hdr = reinterpret_cast<const DataPktHdr*>(frame.ptr);
        const int type = (int)hdr->packetType;
        if (type < (int)PktType::kNumPacketTypes) {
            SendStatusCallback cb = sendStatusHandlers[type].first;
            if (cb) {
                cb(frame.dstMac, frame.ptr + sizeof(DataPktHdr),
                   frame.len - sizeof(DataPktHdr), success,
                   sendStatusHandlers[type].second);
            }
        }
        free(frame.ptr);
    }

    void clearSendQueue() {
        xSemaphoreTake(sendMutex, portMAX_DELAY);
        while (!sendQueue.empty()) {
            free(sendQueue.front().ptr);
            sendQueue.pop();
        }
        xSemaphoreGive(sendMutex);
    }

    /// Releases the slot without reporting, for a teardown where no completion
    /// will clear it. Safe even with a send outstanding: the radio copies the
    /// frame before esp_now_send returns (esp_now.h attention 4), so it holds no
    /// pointer into this buffer.
    void discardInFlight() {
        free(inFlight.ptr);
        inFlight = {};
        // Any verdict the radio recorded belongs to the frame just released.
        // Clearing it stops the next frame being charged with it.
        pendingCompletion.store(Completion::NONE, std::memory_order_relaxed);
    }

    int EnsurePeerIsRegistered(const uint8_t* mac_addr) {
        if (esp_now_is_peer_exist(mac_addr)) return 0;

        // Zero-initialised so a failed call reads as an empty table: that skips
        // the eviction below and falls through to the add, which reports its own
        // error. Returning early instead would skip the registration this was
        // called to do and turn the send into a guaranteed NOT_FOUND.
        esp_now_peer_num_t num_peers = {};
        const esp_err_t countErr = esp_now_get_peer_num(&num_peers);
        if (countErr != ESP_OK)
            LOG_W("ENC", "ESP-NOW peer count unavailable: 0x%X", countErr);
        if (num_peers.total_num >= ESP_NOW_MAX_TOTAL_PEER_NUM) {
            // Full: give up a slot so the send can go out. Which one barely
            // matters, because whoever still wants the evicted MAC re-registers
            // it on its next send through this same function. fetch_peer walks
            // the radio's own list and returns only unicast entries, so the
            // broadcast peer added at init is never a candidate — it has to
            // outlive every unicast, since the send path never re-registers it.
            esp_now_peer_info_t victim = {};
            if (esp_now_fetch_peer(true, &victim) != ESP_OK) {
                LOG_W("ENC", "ESP-NOW peer table full with no unicast peer to drop");
                return -1;
            }
            esp_now_del_peer(victim.peer_addr);
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
    std::vector<std::pair<SendStatusCallback, void*>> sendStatusHandlers;

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

    static constexpr uint8_t MAX_SEND_RETRIES = 5;
    // The frame the radio currently owns; a null ptr means it is idle. Held out of
    // sendQueue so the queue only ever contains unclaimed frames. Main loop only —
    // claim, transmit, retry and release — which is why it needs no lock, and the
    // rule extends to anything that touches it. Add a lock before calling those
    // off the main loop.
    DataSendBuffer inFlight{};
    uint8_t inFlightRetries = 0;

    // The send path's only lock-free cross-task state: what the radio said about
    // the frame it last finished. Nothing is paired with it — the callback gets a destination
    // and the frame's bytes but no handle, and two frames to one peer are
    // indistinguishable — so a verdict rejected on a mismatch would strand the
    // slot with no way to release it.
    std::atomic<Completion> pendingCompletion{Completion::NONE};

    // Frames nobody has claimed yet. sendData is the one entry point a caller off
    // the main loop could reach, so the queue takes a lock even though every
    // caller today is on it.
    std::queue<DataSendBuffer> sendQueue;

    // Storage for rssi, filled from each ESP-NOW receive callback (rx_ctrl).
    // Written on the WiFi task and read from the main loop, so it shares
    // recvMutex with the receive queue that the same callback feeds. Uncapped: an
    // entry costs ~40 bytes on a PSRAM part, and a cap would have to evict a live
    // peer at the device count a real event reaches, leaving it reading
    // RSSI_UNKNOWN — which the proximity tiers treat as the weakest signal.
    std::unordered_map<uint64_t, int> rssiTracker;
};
