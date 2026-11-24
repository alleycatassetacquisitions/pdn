#pragma once

#include <cstdint>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/wireless-types.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "protocol-constants.hpp"

/**
 * Wireless operation modes
 */
enum class WirelessMode {
    ESPNOW,           // Fixed channel, no AP connection - for peer-to-peer communication
    WIFI,             // Connected to AP for HTTP requests
    WIFI_AND_ESPNOW   // FDN: STA for MQTT/HTTP with ESP-NOW up on the AP's channel
};

static const char* WM_TAG = "WirelessManager";

/**
 * WirelessManager coordinates between WiFi (HTTP) and ESP-NOW communication.
 *
 * The ESP32 has a single WiFi radio that can only operate on one channel at a time.
 * When connected to a WiFi AP, the device uses the AP's channel. For ESP-NOW to work
 * reliably between devices, they must all be on the same channel.
 *
 * Modes:
 * - WIFI: HTTP client CONNECTED, ESP-NOW DISCONNECTED
 * - ESPNOW: HTTP client DISCONNECTED, ESP-NOW CONNECTED on ESPNOW_CHANNEL
 * - WIFI_AND_ESPNOW (FDN): both up, on the AP's channel. runOnEspNowChannel()
 *   briefly leaves the AP when that channel is not ESPNOW_CHANNEL.
 *
 * Switching between WIFI and ESPNOW disconnects the active subsystem before
 * connecting the other.
 *
 * This is a pure C++ class that delegates platform-specific operations to the
 * HttpClientInterface and PeerCommsInterface.
 */
class WirelessManager {
public:
    WirelessManager(PeerCommsInterface* peerComms, HttpClientInterface* httpClient)
        : peerComms(peerComms)
        , httpClient(httpClient)
        , currentMode(WirelessMode::ESPNOW) {  // Start in ESP-NOW mode (safer default)
    }

    ~WirelessManager() = default;

    /**
     * Initialize the wireless manager. Should be called once at startup.
     * Ensures the subsystems are in a consistent initial state.
     */
    void initialize() {
        LOG_I(WM_TAG, "Initializing wireless manager...");

        // Start with ESP-NOW active (default mode)
        httpClient->setHttpClientState(HttpClientState::DISCONNECTED);
        peerComms->setPeerCommsState(PeerCommsState::CONNECTED);
        currentMode = WirelessMode::ESPNOW;

        LOG_I(WM_TAG, "Wireless manager initialized in ESP-NOW mode");
    }

    /**
     * Switch to WiFi mode - connects to AP for HTTP requests.
     * This will disconnect ESP-NOW first, then establish WiFi connection.
     * This may take time as it needs to establish connection.
     */
    void enableWifiMode() {
        if (currentMode == WirelessMode::WIFI &&
            httpClient->getHttpClientState() == HttpClientState::CONNECTED) {
            LOG_D(WM_TAG, "Already in WiFi mode");
            return;
        }

        LOG_I(WM_TAG, "Switching to WiFi mode...");

        // Step 1: Disconnect ESP-NOW first to release the WiFi radio
        if (peerComms->getPeerCommsState() == PeerCommsState::CONNECTED) {
            LOG_D(WM_TAG, "Disconnecting ESP-NOW...");
            peerComms->setPeerCommsState(PeerCommsState::DISCONNECTED);
        }

        // Step 2: Connect HTTP client (will establish WiFi AP connection)
        LOG_D(WM_TAG, "Connecting HTTP client...");
        httpClient->setHttpClientState(HttpClientState::CONNECTED);

        // Update mode tracking
        currentMode = WirelessMode::WIFI;

        if (httpClient->isConnected()) {
            LOG_I(WM_TAG, "WiFi mode enabled successfully");
        } else {
            LOG_W(WM_TAG, "WiFi mode enabled but connection pending...");
        }
    }

    /**
     * Switch to ESP-NOW mode - disconnects from AP, sets fixed channel.
     * This will disconnect WiFi/HTTP first, then initialize ESP-NOW.
     * All devices must be in this mode on the same channel for ESP-NOW to work.
     */
    void enablePeerCommsMode() {
        if (currentMode == WirelessMode::ESPNOW &&
            peerComms->getPeerCommsState() == PeerCommsState::CONNECTED) {
            LOG_D(WM_TAG, "Already in ESP-NOW mode");
            return;
        }

        LOG_I(WM_TAG, "Switching to ESP-NOW mode...");

        // Step 1: Disconnect HTTP client first (releases WiFi AP connection but keeps radio on)
        if (httpClient->getHttpClientState() == HttpClientState::CONNECTED) {
            LOG_D(WM_TAG, "Disconnecting HTTP client...");
            httpClient->setHttpClientState(HttpClientState::DISCONNECTED);
        }

        // Step 2: Connect ESP-NOW (will set WiFi to station mode on fixed channel)
        LOG_D(WM_TAG, "Connecting ESP-NOW...");
        peerComms->setPeerCommsState(PeerCommsState::CONNECTED);

        // Update mode tracking
        currentMode = WirelessMode::ESPNOW;

        LOG_I(WM_TAG, "ESP-NOW mode enabled");
    }

    /**
     * FDN concurrent bring-up, step 1: drop ESP-NOW and join the AP. Blocks while
     * the HTTP client connects. Call attachConcurrentEspNow() next; if the STA is
     * associated by then, EspNowDriver keeps the AP link.
     */
    void beginConcurrentWifi() {
        LOG_I(WM_TAG, "Concurrent mode: starting WiFi STA...");
        if (peerComms->getPeerCommsState() == PeerCommsState::CONNECTED) {
            peerComms->setPeerCommsState(PeerCommsState::DISCONNECTED);
        }
        httpClient->setHttpClientState(HttpClientState::CONNECTED);
        currentMode = WirelessMode::WIFI;
    }

    /**
     * FDN concurrent bring-up, step 2: init ESP-NOW. Keeps the AP link if the STA
     * is associated; otherwise ESP-NOW pins ESPNOW_CHANNEL and the HTTP client keeps
     * retrying the AP in the background (ESP-NOW follows it once it joins).
     */
    void attachConcurrentEspNow() {
        LOG_I(WM_TAG, "Concurrent mode: attaching ESP-NOW alongside WiFi...");
        if (peerComms->getPeerCommsState() != PeerCommsState::CONNECTED) {
            peerComms->setPeerCommsState(PeerCommsState::CONNECTED);
        }
        currentMode = WirelessMode::WIFI_AND_ESPNOW;
        LOG_I(WM_TAG, "Concurrent WiFi+ESP-NOW mode enabled");
    }

    /**
     * Get the current wireless mode.
     */
    WirelessMode getCurrentMode() {
        return currentMode;
    }

    /** True when the mode uses WiFi and the STA is connected. */
    bool isWifiConnected() {
        return usesWifi() &&
               httpClient->getHttpClientState() == HttpClientState::CONNECTED &&
               httpClient->isConnected();
    }

    /** True when the mode uses ESP-NOW and it is up. */
    bool isEspNowReady() {
        return usesEspNow() &&
               peerComms->getPeerCommsState() == PeerCommsState::CONNECTED;
    }

    /** True while a queued HTTP request has not had its callback. */
    bool hasQueuedHttpRequests() { return httpClient->hasQueuedRequests(); }

    /**
     * Queue an HTTP request. Switches to WiFi mode first unless WiFi is already in use.
     * @param request The HTTP request to queue; ownership moves to the HTTP client
     * @return true if request was queued successfully
     */
    bool queueHttpRequest(HttpRequest&& request) {
        if (currentMode == WirelessMode::WIFI_AND_ESPNOW) {
            return httpClient->queueRequest(std::move(request));
        }
        if (currentMode != WirelessMode::WIFI) {
            LOG_I(WM_TAG, "Auto-switching to WiFi mode for HTTP request");
            enableWifiMode();
        }

        // Note: We queue the request even if not fully connected yet,
        // the HTTP client will process it once connection is established
        return httpClient->queueRequest(std::move(request));
    }

    /**
     * Send data via ESP-NOW. Switches to ESP-NOW mode first unless ESP-NOW is already in use.
     * @param dst Destination MAC address
     * @param packetType Type of packet
     * @param data Pointer to data buffer
     * @param length Length of data
     * @return 0 on success, negative on error
     */
    int sendEspNowData(const uint8_t* dst, PktType packetType, const uint8_t* data, size_t length) {
        if (!usesEspNow()) {
            LOG_I(WM_TAG, "Auto-switching to ESP-NOW mode for peer communication");
            enablePeerCommsMode();
        }

        if (!isEspNowReady()) {
            LOG_W(WM_TAG, "Cannot send ESP-NOW data - ESP-NOW not ready");
            return -1;
        }

        return peerComms->sendData(dst, packetType, data, length);
    }

    /**
     * Runs `send` (a batch of sendEspNowData calls) with the radio on ESPNOW_CHANNEL,
     * where PDNs listen. Already there: runs it in place. Otherwise leaves the AP,
     * pins ESPNOW_CHANNEL, sends, waits for the queue to drain, then rejoins WiFi
     * if WiFi was in use (MQTT drops and reconnects across the hop).
     */
    template <typename SendFn>
    void runOnEspNowChannel(SendFn&& send) {
        const uint8_t channel = peerComms->getWifiChannel();
        if (isEspNowReady() && channel == ESPNOW_CHANNEL) {
            send();
            return;
        }

        const bool rejoinWifi = usesWifi();
        LOG_I(WM_TAG, "ESP-NOW hop: channel %u -> %u", static_cast<unsigned>(channel),
              static_cast<unsigned>(ESPNOW_CHANNEL));
        if (httpClient->getHttpClientState() == HttpClientState::CONNECTED) {
            httpClient->setHttpClientState(HttpClientState::DISCONNECTED);
        }
        if (peerComms->getPeerCommsState() != PeerCommsState::CONNECTED) {
            peerComms->setPeerCommsState(PeerCommsState::CONNECTED);
        }
        currentMode = WirelessMode::ESPNOW;
        // ESP-NOW may already be up on the AP's channel, in which case connecting it
        // again is a no-op; the explicit pin is what moves the radio.
        if (!peerComms->setWifiChannel(ESPNOW_CHANNEL)) {
            LOG_W(WM_TAG, "ESP-NOW hop: channel pin failed; sending anyway");
        }

        send();
        waitEspNowSendDrain(HOP_DRAIN_MS);

        if (rejoinWifi) {
            rejoinConcurrentWifi();
        }
    }

    /**
     * Set packet handler for ESP-NOW received packets.
     * Note: Handlers can be set regardless of current mode.
     */
    void setEspNowPacketHandler(PktType packetType, PeerCommsInterface::PacketCallback callback, void* ctx) {
        peerComms->setPacketHandler(packetType, callback, ctx);
    }

    /**
     * Clear packet handler for a specific packet type.
     */
    void clearEspNowPacketHandler(PktType packetType) {
        peerComms->clearPacketHandler(packetType);
    }

    /**
     * Register a MAC as an ESP-NOW peer, making it eligible for unicast sends.
     */
    int addEspNowPeer(const uint8_t* mac) {
        return peerComms->addEspNowPeer(mac);
    }

    /**
     * Remove a MAC from the ESP-NOW peer table.
     */
    int removeEspNowPeer(const uint8_t* mac) {
        return peerComms->removeEspNowPeer(mac);
    }

    /**
     * Get the device's MAC address.
     */
    uint8_t* getMacAddress() {
        return peerComms->getMacAddress();
    }

    /**
     * Get the ESP-NOW broadcast address.
     */
    const uint8_t* getBroadcastAddress() {
        return peerComms->getGlobalBroadcastAddress();
    }

    /**
     * Must be called in the main loop to process wireless operations.
     * Delegates to the underlying drivers.
     */
    void exec() {
        // The drivers handle their own processing through their exec() methods
        // called by the DriverManager. This method is here for any future
        // coordination logic the WirelessManager might need.
    }

    /**
     * Get a string representation of the current wireless state.
     * Useful for debugging.
     */
    const char* getStateString() {
        switch (currentMode) {
            case WirelessMode::WIFI:
                return httpClient->isConnected() ? "WIFI_CONNECTED" : "WIFI_CONNECTING";
            case WirelessMode::ESPNOW:
                return "ESPNOW_ACTIVE";
            case WirelessMode::WIFI_AND_ESPNOW:
                return httpClient->isConnected() ? "WIFI_ESPNOW_CONNECTED" : "WIFI_ESPNOW_CONNECTING";
            default:
                return "UNKNOWN";
        }
    }

private:
    static constexpr unsigned HOP_DRAIN_MS = 500;

    bool usesWifi() const {
        return currentMode == WirelessMode::WIFI || currentMode == WirelessMode::WIFI_AND_ESPNOW;
    }

    bool usesEspNow() const {
        return currentMode == WirelessMode::ESPNOW ||
               currentMode == WirelessMode::WIFI_AND_ESPNOW;
    }

    /**
     * Back to WIFI_AND_ESPNOW after a hop. ESP-NOW is re-attached even when the AP
     * does not come back in time, so peers stay reachable while the HTTP client
     * keeps retrying.
     */
    void rejoinConcurrentWifi() {
        beginConcurrentWifi();
        attachConcurrentEspNow();
        if (isWifiConnected()) {
            LOG_I(WM_TAG, "ESP-NOW hop: rejoined WiFi on channel %u",
                  static_cast<unsigned>(peerComms->getWifiChannel()));
        } else {
            LOG_W(WM_TAG, "ESP-NOW hop: WiFi not back yet; ESP-NOW up, STA retrying");
        }
    }

    void waitEspNowSendDrain(unsigned timeoutMs) {
#ifdef ESP_PLATFORM
        const TickType_t deadline =
            xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
        while (!peerComms->isSendQueueEmpty() && xTaskGetTickCount() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!peerComms->isSendQueueEmpty()) {
            LOG_W(WM_TAG, "ESP-NOW hop: send queue not empty after %ums",
                  timeoutMs);
        }
#else
        (void)timeoutMs;
#endif
    }

    PeerCommsInterface* peerComms;
    HttpClientInterface* httpClient;
    WirelessMode currentMode;
};
