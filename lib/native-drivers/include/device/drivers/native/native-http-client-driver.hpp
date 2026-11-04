#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstring>
#include <queue>
#include <deque>

/**
 * Structure to track HTTP request/response history for display.
 */
struct HttpRequestHistoryEntry {
    std::string method;
    std::string path;
    std::string requestBody;
    std::string responseBody;
    int statusCode;
    bool success;
};

class NativeHttpClientDriver : public HttpClientDriverInterface {
public:
    /** @p name is passed through to the driver interface base. */
    explicit NativeHttpClientDriver(const std::string& name)
        : HttpClientDriverInterface(name) {
        // Generate a fake MAC address for this instance
        for (int i = 0; i < 6; i++) {
            macAddress[i] = static_cast<uint8_t>(rand() % 256);
        }
        // Set local MAC prefix
        macAddress[0] = 0x02;  // Locally administered
    }

    ~NativeHttpClientDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // Process pending requests through mock server
        processPendingRequests();
    }

    void setWifiConfig(WifiConfig* config) override {
        if (config) {
            wifiConfig = *config;
        }
    }

    /** @return true when the mock WiFi connection state is CONNECTED. */
    bool isConnected() override { return httpClientState == HttpClientState::CONNECTED; }

    /** True until every queued request has had its callback. */
    bool hasQueuedRequests() override { return !pendingRequests.empty(); }

    /** Takes ownership of @p request; a large payload is moved, not copied. */
    bool queueRequest(HttpRequest&& request) override {
        // Queue the request for processing in exec()
        pendingRequests.push(std::move(request));
        return true;
    }

    void disconnect() override {
        httpClientState = HttpClientState::DISCONNECTED;
        // As the ESP32 driver does: every request still queued belongs to the
        // WiFi trip that is ending.
        while (!pendingRequests.empty()) {
            HttpRequest request = std::move(pendingRequests.front());
            pendingRequests.pop();
            if (request.onError) {
                request.onError({WirelessError::CONNECTION_FAILED, "Disconnected before an answer"});
            }
        }
    }

    void updateConfig(WifiConfig* config) override {
        setWifiConfig(config);
    }

    void retryConnection() override {
        // Simulate successful reconnection
        if (httpClientState == HttpClientState::DISCONNECTED) {
            httpClientState = HttpClientState::CONNECTED;
        }
    }

    uint8_t* getMacAddress() override {
        return macAddress;
    }

    void setHttpClientState(HttpClientState state) override {
        if (state == HttpClientState::DISCONNECTED) {
            disconnect();
        } else {
            httpClientState = state;
        }
    }

    HttpClientState getHttpClientState() override {
        return httpClientState;
    }

    /**
     * Get request/response history for CLI display.
     */
    const std::deque<HttpRequestHistoryEntry>& getRequestHistory() const {
        return requestHistory_;
    }

    /**
     * Get the number of pending requests.
     */
    size_t getPendingRequestCount() const {
        return pendingRequests.size();
    }

private:
    WifiConfig wifiConfig;
    uint8_t macAddress[6];
    HttpClientState httpClientState = HttpClientState::DISCONNECTED;

    std::queue<HttpRequest> pendingRequests;
    std::deque<HttpRequestHistoryEntry> requestHistory_;
    static constexpr size_t MAX_HISTORY = 5;

    void addToHistory(const HttpRequestHistoryEntry& entry) {
        requestHistory_.push_back(entry);
        while (requestHistory_.size() > MAX_HISTORY) {
            requestHistory_.pop_front();
        }
    }

    /**
     * Process all pending HTTP requests.
     */
    void processPendingRequests();
};
