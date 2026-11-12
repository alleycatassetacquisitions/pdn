#pragma once

#include <cstdint>
#include <utility>
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/reliable-transport.hpp"
#include "wireless/wireless-types.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "protocol-constants.hpp"

/**
 * Wireless operation modes
 */
enum class WirelessMode {
    ESPNOW,  // Fixed channel, no AP connection - for peer-to-peer communication
    WIFI     // Connected to AP for HTTP requests
};

static const char* WM_TAG = "WirelessManager";

/**
 * WirelessManager coordinates between WiFi (HTTP) and ESP-NOW communication,
 * and owns the device's one ReliableTransport, so every reliable channel on the
 * device shares one retry queue and one pump.
 *
 * The ESP32 has a single WiFi radio that can only operate on one channel at a time.
 * When connected to a WiFi AP, the device uses the AP's channel. For ESP-NOW to work
 * reliably between devices, they must all be on the same channel.
 *
 * This manager handles switching between modes using explicit state management:
 * - WIFI mode: HTTP client CONNECTED, ESP-NOW DISABLED
 * - ESPNOW mode: HTTP client DISCONNECTED, ESP-NOW ENABLED
 *
 * State transitions are performed atomically:
 * 1. Disable or disconnect the currently active subsystem first
 * 2. Enable or connect the target subsystem
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

    /** Initializes the wireless manager in ESP-NOW peer comms mode. */
    void initialize() {
        LOG_I(WM_TAG, "Initializing wireless manager...");

        httpClient->setHttpClientState(HttpClientState::DISCONNECTED);
        peerComms->setPeerCommsState(PeerCommsState::ENABLED);
        currentMode = WirelessMode::ESPNOW;

        LOG_I(WM_TAG, "Wireless manager initialized in ESP-NOW mode");
    }

    /** Disables ESP-NOW and connects the HTTP client over WiFi. */
    void enableWifiMode() {
        if (currentMode == WirelessMode::WIFI &&
            httpClient->getHttpClientState() == HttpClientState::CONNECTED) {
            LOG_D(WM_TAG, "Already in WiFi mode");
            return;
        }

        LOG_I(WM_TAG, "Switching to WiFi mode...");

        if (peerComms->getPeerCommsState() == PeerCommsState::ENABLED) {
            LOG_D(WM_TAG, "Disabling ESP-NOW...");
            peerComms->setPeerCommsState(PeerCommsState::DISABLED);
        }

        LOG_D(WM_TAG, "Connecting HTTP client...");
        httpClient->setHttpClientState(HttpClientState::CONNECTED);

        currentMode = WirelessMode::WIFI;

        if (httpClient->isConnected()) {
            LOG_I(WM_TAG, "WiFi mode enabled successfully");
        } else {
            LOG_W(WM_TAG, "WiFi mode enabled but connection pending...");
        }
    }

    /** Disconnects the HTTP client and enables ESP-NOW on the fixed channel. */
    void enablePeerCommsMode() {
        if (currentMode == WirelessMode::ESPNOW &&
            peerComms->getPeerCommsState() == PeerCommsState::ENABLED) {
            LOG_D(WM_TAG, "Already in ESP-NOW mode");
            return;
        }

        LOG_I(WM_TAG, "Switching to ESP-NOW mode...");

        if (httpClient->getHttpClientState() == HttpClientState::CONNECTED) {
            LOG_D(WM_TAG, "Disconnecting HTTP client...");
            httpClient->setHttpClientState(HttpClientState::DISCONNECTED);
        }

        LOG_D(WM_TAG, "Enabling ESP-NOW...");
        peerComms->setPeerCommsState(PeerCommsState::ENABLED);

        currentMode = WirelessMode::ESPNOW;

        LOG_I(WM_TAG, "ESP-NOW mode enabled");
    }

    /** Returns the current wireless mode. */
    WirelessMode getCurrentMode() {
        return currentMode;
    }

    /** True when ESP-NOW mode is active and peer comms is enabled. */
    bool isEspNowReady() {
        return currentMode == WirelessMode::ESPNOW &&
               peerComms->getPeerCommsState() == PeerCommsState::ENABLED;
    }

    /** True while a queued HTTP request has not had its callback. */
    bool hasQueuedHttpRequests() { return httpClient->hasQueuedRequests(); }

    /** Queues an HTTP request, automatically switching to WiFi mode if needed. */
    bool queueHttpRequest(HttpRequest&& request) {
        if (currentMode != WirelessMode::WIFI) {
            LOG_I(WM_TAG, "Auto-switching to WiFi mode for HTTP request");
            enableWifiMode();
        }

        // Note: We queue the request even if not fully connected yet,
        // the HTTP client will process it once connection is established
        return httpClient->queueRequest(std::move(request));
    }

    /** Sends a peer-to-peer packet via ESP-NOW, rejecting sends while WiFi holds the radio. */
    int sendEspNowData(const uint8_t* dst, PktType packetType, const uint8_t* data, size_t length) {
        if (!isEspNowReady()) {
            LOG_W(WM_TAG, "Cannot send ESP-NOW data - ESP-NOW not ready");
            return -1;
        }

        return peerComms->sendData(dst, packetType, data, length);
    }

    /** Registers a handler for incoming ESP-NOW packets of the specified type. */
    void setEspNowPacketHandler(PktType packetType, PeerCommsInterface::PacketCallback callback, void* ctx) {
        peerComms->setPacketHandler(packetType, std::move(callback), ctx);
    }

    /**
     * Registers the per-packet MAC-layer SEND_SUCCESS/FAIL callback for a packet
     * type; a unicast reliable send is cleared by it.
     */
    void setEspNowSendStatusHandler(PktType packetType, PeerCommsInterface::SendStatusCallback callback, void* ctx) {
        peerComms->setSendStatusHandler(packetType, callback, ctx);
    }

    /** Clears the packet handler for the specified packet type. */
    void clearEspNowPacketHandler(PktType packetType) { peerComms->clearPacketHandler(packetType); }

    /** Clears the send-status handler for the specified packet type. */
    void clearEspNowSendStatusHandler(PktType packetType) {
        peerComms->clearSendStatusHandler(packetType);
    }

    /** Returns the device's MAC address. */
    uint8_t* getMacAddress() {
        return peerComms->getMacAddress();
    }

    /** Returns the ESP-NOW broadcast address. */
    const uint8_t* getBroadcastAddress() {
        return peerComms->getGlobalBroadcastAddress();
    }

    /** The reliable transport every channel on this device shares. */
    ReliableTransport* getReliableTransport() { return &reliableTransport; }

    /** Drives reliable retransmits and abandonment; called once per tick by Device::loop(). */
    void sync() { reliableTransport.sync(); }

    /** Returns a debug string representing the current wireless state. */
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
    // Last: its constructor registers the kReliableAck handler through peerComms.
    ReliableTransport reliableTransport{this};
};
