#pragma once

#include <cstdint>
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/wireless-types.hpp"
#include "device/drivers/peer-comms-types.hpp"

enum class WirelessMode {
    ESPNOW,  // Fixed channel, no AP connection - for peer-to-peer communication
    WIFI     // Connected to AP for HTTP requests
};

static const char* WM_TAG = "WirelessManager";

// The ESP32 has a single WiFi radio on one channel at a time: a WiFi AP
// connection forces the AP's channel, but ESP-NOW needs all devices on a shared
// channel. So only one mode is active at a time, and switching disconnects the
// current subsystem before connecting the target. WiFi is always a brief
// excursion that must end in enablePeerCommsMode(): an excursion left running
// (or scanning for an absent AP) drags the radio off ESPNOW_CHANNEL and jams
// every nearby device's game.
class WirelessManager {
public:
    WirelessManager(PeerCommsInterface* peerComms, HttpClientInterface* httpClient)
        : peerComms(peerComms)
        , httpClient(httpClient)
        , currentMode(WirelessMode::ESPNOW) {  // ESP-NOW is the safer default
    }

    ~WirelessManager() = default;

    void initialize() {
        LOG_I(WM_TAG, "Initializing wireless manager...");

        httpClient->setHttpClientState(HttpClientState::IDLE);
        peerComms->setPeerCommsState(PeerCommsState::CONNECTED);
        currentMode = WirelessMode::ESPNOW;
        
        LOG_I(WM_TAG, "Wireless manager initialized in ESP-NOW mode");
    }
    
    // Disconnect ESP-NOW, then start the AP connection for HTTP. Returns
    // immediately; the HTTP client's exec() completes the connection and
    // isWifiConnected() turns true once it's up.
    void enableWifiMode() {
        if (currentMode == WirelessMode::WIFI && 
            httpClient->getHttpClientState() == HttpClientState::WIFI_ENGAGED) {
            LOG_D(WM_TAG, "Already in WiFi mode");
            return;
        }
        
        LOG_I(WM_TAG, "Switching to WiFi mode...");

        if (peerComms->getPeerCommsState() == PeerCommsState::CONNECTED) {
            LOG_D(WM_TAG, "Disconnecting ESP-NOW...");
            peerComms->setPeerCommsState(PeerCommsState::DISCONNECTED);
        }

        LOG_D(WM_TAG, "Connecting HTTP client...");
        httpClient->setHttpClientState(HttpClientState::WIFI_ENGAGED);

        currentMode = WirelessMode::WIFI;
        
        if (httpClient->isConnected()) {
            LOG_I(WM_TAG, "WiFi mode enabled successfully");
        } else {
            LOG_W(WM_TAG, "WiFi mode enabled but connection pending...");
        }
    }
    
    // Disconnect WiFi/HTTP, then initialize ESP-NOW on the fixed channel. All
    // devices must be in this mode on the same channel for ESP-NOW to work.
    void enablePeerCommsMode() {
        if (currentMode == WirelessMode::ESPNOW && 
            peerComms->getPeerCommsState() == PeerCommsState::CONNECTED) {
            LOG_D(WM_TAG, "Already in ESP-NOW mode");
            return;
        }
        
        LOG_I(WM_TAG, "Switching to ESP-NOW mode...");

        // Disconnect HTTP first: releases the AP connection but keeps the radio on.
        if (httpClient->getHttpClientState() == HttpClientState::WIFI_ENGAGED) {
            LOG_D(WM_TAG, "Disconnecting HTTP client...");
            httpClient->setHttpClientState(HttpClientState::IDLE);
        }

        LOG_D(WM_TAG, "Connecting ESP-NOW...");
        peerComms->setPeerCommsState(PeerCommsState::CONNECTED);

        currentMode = WirelessMode::ESPNOW;
        
        LOG_I(WM_TAG, "ESP-NOW mode enabled");
    }
    
    WirelessMode getCurrentMode() {
        return currentMode;
    }

    bool isWifiConnected() {
        return currentMode == WirelessMode::WIFI && 
               httpClient->getHttpClientState() == HttpClientState::WIFI_ENGAGED &&
               httpClient->isConnected();
    }
    
    bool isEspNowReady() {
        return currentMode == WirelessMode::ESPNOW && 
               peerComms->getPeerCommsState() == PeerCommsState::CONNECTED;
    }
    
    // Queues even if not yet connected; the HTTP client processes it once up.
    bool queueHttpRequest(HttpRequest& request) {
        if (currentMode != WirelessMode::WIFI) {
            LOG_I(WM_TAG, "Auto-switching to WiFi mode for HTTP request");
            enableWifiMode();
        }
        return httpClient->queueRequest(request);
    }
    
    // Returns 0 on success, negative on error.
    int sendEspNowData(const uint8_t* dst, PktType packetType, const uint8_t* data, size_t length) {
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
    
    // Handlers can be set regardless of current mode.
    void setEspNowPacketHandler(PktType packetType, PeerCommsInterface::PacketCallback callback, void* ctx) {
        peerComms->setPacketHandler(packetType, callback, ctx);
    }

    void clearEspNowPacketHandler(PktType packetType) {
        peerComms->clearPacketHandler(packetType);
    }

    int addEspNowPeer(const uint8_t* mac) {
        return peerComms->addEspNowPeer(mac);
    }

    int removeEspNowPeer(const uint8_t* mac) {
        return peerComms->removeEspNowPeer(mac);
    }

    uint8_t* getMacAddress() {
        return peerComms->getMacAddress();
    }

    const uint8_t* getBroadcastAddress() {
        return peerComms->getGlobalBroadcastAddress();
    }

    // Drivers self-process via their own exec()s; placeholder for future
    // WirelessManager-level coordination.
    void exec() {
    }

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
