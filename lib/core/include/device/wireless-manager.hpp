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
    WIFI_AND_ESPNOW   // FDN: STA for MQTT/HTTP while ESP-NOW stays up (page hop if AP != ESPNOW_CHANNEL)
};

static const char* WM_TAG = "WirelessManager";

/**
 * WirelessManager coordinates between WiFi (HTTP) and ESP-NOW communication.
 *
 * The ESP32 has a single WiFi radio that can only operate on one channel at a time.
 * When connected to a WiFi AP, the device uses the AP's channel. For ESP-NOW to work
 * reliably between devices, they must all be on the same channel.
 *
 * This manager handles switching between modes using explicit state management:
 * - WIFI mode: HTTP client CONNECTED, ESP-NOW DISCONNECTED
 * - ESPNOW mode: HTTP client DISCONNECTED, ESP-NOW CONNECTED
 *
 * State transitions are performed atomically:
 * 1. Disconnect the currently active subsystem first
 * 2. Connect the target subsystem
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
     * FDN concurrent bring-up, step 1: start STA without touching ESP-NOW.
     * Call attachConcurrentEspNow() only after isWifiConnected() (or IP) is true
     * so EspNowDriver keeps the AP association.
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
     * FDN concurrent bring-up, step 2: init ESP-NOW while STA stays associated.
     * Page relays hop to ESPNOW_CHANNEL when the AP is elsewhere; other ESP-NOW
     * traffic still prefers a venue AP on ESPNOW_CHANNEL until dual-radio hardware.
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

    /**
     * Check if WiFi is connected (only relevant in WIFI mode).
     */
    bool isWifiConnected() {
        const bool wifiMode = currentMode == WirelessMode::WIFI ||
                              currentMode == WirelessMode::WIFI_AND_ESPNOW;
        return wifiMode &&
               httpClient->getHttpClientState() == HttpClientState::CONNECTED &&
               httpClient->isConnected();
    }

    /**
     * Check if ESP-NOW is active and ready.
     */
    bool isEspNowReady() {
        const bool espMode = currentMode == WirelessMode::ESPNOW ||
                             currentMode == WirelessMode::WIFI_AND_ESPNOW;
        return espMode &&
               peerComms->getPeerCommsState() == PeerCommsState::CONNECTED;
    }

    /** True while a queued HTTP request has not had its callback. */
    bool hasQueuedHttpRequests() { return httpClient->hasQueuedRequests(); }

    /**
     * Queue an HTTP request. Automatically switches to WiFi mode if needed.
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
     * Send data via ESP-NOW. Automatically switches to ESP-NOW mode if needed.
     * @param dst Destination MAC address
     * @param packetType Type of packet
     * @param data Pointer to data buffer
     * @param length Length of data
     * @return 0 on success, negative on error
     */
    int sendEspNowData(const uint8_t* dst, PktType packetType, const uint8_t* data, size_t length) {
        if (currentMode != WirelessMode::ESPNOW &&
            currentMode != WirelessMode::WIFI_AND_ESPNOW) {
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
     * FDN page relay: broadcast opaque PageCmd bytes on ESPNOW_CHANNEL.
     * Fast path when already on that channel; otherwise hop off WiFi, send, restore
     * concurrent MQTT/STA (brief MQTT disconnect on non-home-channel APs).
     */
    int sendEspNowPageRelay(const uint8_t* data, size_t length) {
        const uint8_t* broadcast = getBroadcastAddress();
        if (broadcast == nullptr) {
            return -1;
        }

        const uint8_t channel = peerComms->getWifiChannel();
        const bool onHomeChannel = (channel == ESPNOW_CHANNEL);
        const bool canSendNow =
            isEspNowReady() &&
            (onHomeChannel || currentMode == WirelessMode::ESPNOW);

        if (canSendNow && onHomeChannel) {
            LOG_I(WM_TAG, "Page relay fast path on channel %u",
                  static_cast<unsigned>(channel));
            return sendEspNowData(broadcast, PktType::kPageCommand, data, length);
        }
        if (canSendNow && currentMode == WirelessMode::ESPNOW) {
            LOG_I(WM_TAG, "Page relay on ESP-NOW-only (channel %u)",
                  static_cast<unsigned>(channel));
            return sendEspNowData(broadcast, PktType::kPageCommand, data, length);
        }

        const bool restoreConcurrent =
            currentMode == WirelessMode::WIFI_AND_ESPNOW ||
            currentMode == WirelessMode::WIFI;

        LOG_I(WM_TAG, "Page relay hop: leave WiFi (channel %u) → ESPNOW_CHANNEL %u",
              static_cast<unsigned>(channel), static_cast<unsigned>(ESPNOW_CHANNEL));
        enablePeerCommsMode();

        const int rc = sendEspNowData(broadcast, PktType::kPageCommand, data, length);
        waitEspNowSendDrain(PAGE_RELAY_DRAIN_MS);

        if (restoreConcurrent) {
            LOG_I(WM_TAG, "Page relay restore: rejoining WiFi for concurrent MQTT");
            beginConcurrentWifi();
            if (isWifiConnected()) {
                attachConcurrentEspNow();
                LOG_I(WM_TAG, "Page relay restore ok (channel %u)",
                      static_cast<unsigned>(peerComms->getWifiChannel()));
            } else {
                LOG_W(WM_TAG, "Page relay restore: WiFi not up after hop; staying ESP-NOW");
            }
        }

        return rc;
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
    static constexpr unsigned PAGE_RELAY_DRAIN_MS = 500;

    void waitEspNowSendDrain(unsigned timeoutMs) {
#ifdef ESP_PLATFORM
        const TickType_t deadline =
            xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
        while (!peerComms->isSendQueueEmpty() && xTaskGetTickCount() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!peerComms->isSendQueueEmpty()) {
            LOG_W(WM_TAG, "Page relay: ESP-NOW send queue not empty after %ums",
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
