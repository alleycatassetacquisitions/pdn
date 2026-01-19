#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstring>

class NativeHttpClientDriver : public HttpClientDriverInterface {
public:
    NativeHttpClientDriver(std::string name) : HttpClientDriverInterface(name) {
        // Generate a fake MAC address for this instance
        for (int i = 0; i < 6; i++) {
            macAddress_[i] = static_cast<uint8_t>(rand() % 256);
        }
        // Set local MAC prefix
        macAddress_[0] = 0x02; // Locally administered
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
            wifiConfig_ = *config;
        }
    }

    bool isConnected() override {
        // Stub always reports disconnected - can be toggled for testing
        return connected_;
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
        connected_ = false;
    }

    void updateConfig(WifiConfig* config) override {
        setWifiConfig(config);
    }

    void retryConnection() override {
        // No-op for stub
    }

    uint8_t* getMacAddress() override {
        return macAddress_;
    }

    // Test helper methods
    void setConnected(bool connected) { connected_ = connected; }

private:
    WifiConfig wifiConfig_;
    bool connected_ = false;
    uint8_t macAddress_[6];
};
