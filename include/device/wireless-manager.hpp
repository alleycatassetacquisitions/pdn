#pragma once

#include <cstdint>
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/wireless-types.hpp"
#include "wireless/peer-comms-types.hpp"
#include "device/device-constants.hpp"

/**
 * Wireless operation modes
 */
enum class WirelessMode {
    ESPNOW,  // Fixed channel, no AP connection - for peer-to-peer communication
    WIFI     // Connected to AP for HTTP requests
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

        LOG_I(WM_TAG, "Switching to ESP-NOW mode on channel %d...", ESPNOW_CHANNEL);

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

        LOG_I(WM_TAG, "ESP-NOW mode enabled on channel %d", ESPNOW_CHANNEL);
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
        return currentMode == WirelessMode::WIFI &&
               httpClient->getHttpClientState() == HttpClientState::CONNECTED &&
               httpClient->isConnected();
    }

    /**
     * Check if ESP-NOW is active and ready.
     */
    bool isEspNowReady() {
        return currentMode == WirelessMode::ESPNOW &&
               peerComms->getPeerCommsState() == PeerCommsState::CONNECTED;
    }

    /**
     * Queue an HTTP request. Automatically switches to WiFi mode if needed.
     * @param request The HTTP request to queue
     * @return true if request was queued successfully
     */
    bool queueHttpRequest(HttpRequest& request) {
        // Auto-switch to WiFi mode if needed
        if (currentMode != WirelessMode::WIFI) {
            LOG_I(WM_TAG, "Auto-switching to WiFi mode for HTTP request");
            enableWifiMode();
        }

        // Note: We queue the request even if not fully connected yet,
        // the HTTP client will process it once connection is established
        return httpClient->queueRequest(request);
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
        // Auto-switch to ESP-NOW mode if needed
        if (currentMode != WirelessMode::ESPNOW) {
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
            default:
                return "UNKNOWN";
        }
    }

private:
    PeerCommsInterface* peerComms;
    HttpClientInterface* httpClient;
    WirelessMode currentMode;
};
