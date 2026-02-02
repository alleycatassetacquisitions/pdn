#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstring>

class NativeHttpClientDriver : public HttpClientDriverInterface {
public:
    NativeHttpClientDriver(std::string name) : HttpClientDriverInterface(name) {
        // Generate a fake MAC address for this instance
        for (int i = 0; i < 6; i++) {
            macAddress[i] = static_cast<uint8_t>(rand() % 256);
        }
        // Set local MAC prefix
        macAddress[0] = 0x02; // Locally administered
    }

    ~NativeHttpClientDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // Process any pending requests - for stub, just call error callback
        // In a real implementation, this would use libcurl or similar
    }

    void setWifiConfig(WifiConfig* config) override {
        if (config) {
            wifiConfig = *config;
        }
    }

    bool isConnected() override {
        // Stub always reports disconnected - can be toggled for testing
        return connected && httpClientState == HttpClientState::CONNECTED;
    }

    bool queueRequest(HttpRequest& request) override {
        // Stub implementation - immediately fail with appropriate error
        if (request.onError) {
            WirelessErrorInfo error;
            error.code = WirelessError::WIFI_NOT_CONNECTED;
            error.message = "Native stub: WiFi not available";
            error.willRetry = false;
            request.onError(error);
        }
        return false;
    }

    void disconnect() override {
        connected = false;
        httpClientState = HttpClientState::DISCONNECTED;
    }

    void updateConfig(WifiConfig* config) override {
        setWifiConfig(config);
    }

    void retryConnection() override {
        // No-op for stub
    }

    uint8_t* getMacAddress() override {
        return macAddress;
    }

    void setHttpClientState(HttpClientState state) override {
        if (state == HttpClientState::CONNECTED && httpClientState != HttpClientState::CONNECTED) {
            // Simulate connection
            connected = true;
            httpClientState = HttpClientState::CONNECTED;
        } else if (state == HttpClientState::DISCONNECTED && httpClientState != HttpClientState::DISCONNECTED) {
            disconnect();
        }
    }

    HttpClientState getHttpClientState() override {
        return httpClientState;
    }

    // Test helper methods
    void setConnected(bool isConnected) { connected = isConnected; }
    uint8_t getCurrentChannel() const { return currentChannel; }

private:
    WifiConfig wifiConfig;
    bool connected = false;
    uint8_t currentChannel = 0;
    uint8_t macAddress[6];
    HttpClientState httpClientState = HttpClientState::DISCONNECTED;
};
