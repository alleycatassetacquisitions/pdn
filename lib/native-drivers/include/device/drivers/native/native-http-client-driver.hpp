#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstring>
#include <queue>
#include <deque>

// Forward declaration - the mock server is only used in CLI builds
#ifdef NATIVE_BUILD
namespace cli {
    class MockHttpServer;
}
#endif

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
    explicit NativeHttpClientDriver(const std::string& name) : HttpClientDriverInterface(name) {
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
        // Process pending requests through mock server
        processPendingRequests();
    }

    bool isConnected() override {
        return connected && httpClientState == HttpClientState::WIFI_ENGAGED;
    }

    bool queueRequest(HttpRequest& request) override {
        // Queue the request for processing in exec()
        pendingRequests_.push(request);
        return true;
    }

    void disconnect() override {
        connected = false;
        httpClientState = HttpClientState::IDLE;
    }

    uint8_t* getMacAddress() override {
        return macAddress;
    }

    void setHttpClientState(HttpClientState state) override {
        if (state == HttpClientState::WIFI_ENGAGED && httpClientState != HttpClientState::WIFI_ENGAGED) {
            // Simulate connection
            connected = true;
            httpClientState = HttpClientState::WIFI_ENGAGED;
        } else if (state == HttpClientState::IDLE && httpClientState != HttpClientState::IDLE) {
            disconnect();
        }
    }

    HttpClientState getHttpClientState() override {
        return httpClientState;
    }

    // Test helper methods
    void setConnected(bool isConnected) {
        connected = isConnected;
        if (isConnected) {
            httpClientState = HttpClientState::WIFI_ENGAGED;
        }
    }

    /**
     * Enable or disable mock server mode.
     * When enabled, requests are processed through the CLI MockHttpServer.
     * When disabled, requests fail immediately (original behavior).
     */
    void setMockServerEnabled(bool enabled) {
        mockServerEnabled_ = enabled;
    }
    
    bool isMockServerEnabled() const {
        return mockServerEnabled_;
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
        return pendingRequests_.size();
    }

private:
    bool connected = false;
    uint8_t macAddress[6];
    HttpClientState httpClientState = HttpClientState::IDLE;
    
    std::queue<HttpRequest> pendingRequests_;
    std::deque<HttpRequestHistoryEntry> requestHistory_;
    static constexpr size_t MAX_HISTORY = 5;
    bool mockServerEnabled_ = false;
    
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
