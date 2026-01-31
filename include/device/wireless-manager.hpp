#pragma once

#include <cstdint>
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/wireless-types.hpp"
#include "wireless/peer-comms-types.hpp"

/**
 * Wireless operation modes
 */
enum class WirelessMode {
    ESPNOW,  // Fixed channel, no AP connection - for peer-to-peer communication
    WIFI     // Connected to AP for HTTP requests
};

// Fixed channel for ESP-NOW communication
// All devices must use the same channel for reliable ESP-NOW
static constexpr uint8_t ESPNOW_CHANNEL = 6;

static const char* WM_TAG = "WirelessManager";

/**
 * WirelessManager coordinates between WiFi (HTTP) and ESP-NOW communication.
 * 
 * The ESP32 has a single WiFi radio that can only operate on one channel at a time.
 * When connected to a WiFi AP, the device uses the AP's channel. For ESP-NOW to work
 * reliably between devices, they must all be on the same channel.
 * 
 * This manager handles switching between modes:
 * - WIFI mode: Connected to AP for HTTP requests
 * - ESPNOW mode: Disconnected from AP, forced to channel 6 for peer communication
 * 
 * This is a pure C++ class that delegates platform-specific operations to the
 * HttpClientInterface and PeerCommsInterface.
 */
class WirelessManager {
public:
    WirelessManager(PeerCommsInterface* peerComms, HttpClientInterface* httpClient)
        : peerComms(peerComms)
        , httpClient(httpClient)
        , currentMode(WirelessMode::WIFI) {
    }

    ~WirelessManager() = default;
    
    /**
     * Switch to WiFi mode - connects to AP for HTTP requests.
     * This may take time as it needs to establish connection.
     */
    void enableWifiMode() {
        if (currentMode == WirelessMode::WIFI) {
            LOG_D(WM_TAG, "Already in WiFi mode");
            return;
        }
        
        LOG_I(WM_TAG, "Switching to WiFi mode...");
        
        if (httpClient->enableHttpMode()) {
            currentMode = WirelessMode::WIFI;
            LOG_I(WM_TAG, "WiFi mode enabled");
        } else {
            LOG_W(WM_TAG, "Failed to enable WiFi mode");
        }
    }
    
    /**
     * Switch to ESP-NOW mode - disconnects from AP, sets fixed channel.
     * All devices must be in this mode on the same channel for ESP-NOW to work.
     */
    void enablePeerCommsMode() {
        if (currentMode == WirelessMode::ESPNOW) {
            LOG_D(WM_TAG, "Already in ESP-NOW mode");
            return;
        }
        
        LOG_I(WM_TAG, "Switching to ESP-NOW mode on channel %d...", ESPNOW_CHANNEL);
        
        httpClient->enablePeerCommsMode(ESPNOW_CHANNEL);
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
        return currentMode == WirelessMode::WIFI && httpClient->isConnected();
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
        
        if (!isWifiConnected()) {
            LOG_W(WM_TAG, "Cannot queue HTTP request - WiFi not connected");
            return false;
        }
        
        return httpClient->queueRequest(request);
    }
    
    /**
     * Send data via ESP-NOW. Must be in ESPNOW mode.
     * @param dst Destination MAC address
     * @param packetType Type of packet
     * @param data Pointer to data buffer
     * @param length Length of data
     * @return 0 on success, negative on error
     */
    int sendEspNowData(const uint8_t* dst, PktType packetType, const uint8_t* data, size_t length) {
        if (currentMode != WirelessMode::ESPNOW) {
            LOG_W(WM_TAG, "Cannot send ESP-NOW data - not in ESP-NOW mode. Call enablePeerCommsMode() first.");
            return -1;
        }
        
        return peerComms->sendData(dst, packetType, data, length);
    }
    
    /**
     * Set packet handler for ESP-NOW received packets.
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
     */
    void exec() {
        // Nothing to do here - HTTP client and peer comms have their own exec
    }

private:
    PeerCommsInterface* peerComms;
    HttpClientInterface* httpClient;
    WirelessMode currentMode;
};
