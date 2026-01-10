#pragma once

#include <WiFi.h>
#include <esp_http_client.h>
#include <queue>
#include "http-client-interface.hpp"
#include "wireless-types.hpp"
#include "utils/simple-timer.hpp"

// Forward declaration for the event handler
class Esp32S3HttpClient;
esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt);

/**
 * ESP32-S3 implementation of HttpClientInterface.
 * Handles WiFi connection and HTTP requests using ESP-IDF's esp_http_client.
 * 
 * This class manages:
 * - WiFi connection establishment and monitoring
 * - HTTP client initialization and cleanup
 * - Asynchronous request queueing and processing
 * - Retry logic with exponential backoff
 */
class Esp32S3HttpClient : public HttpClientInterface {
public:
    Esp32S3HttpClient();
    ~Esp32S3HttpClient() override;

    /**
     * Initialize WiFi connection with the provided configuration.
     * Non-blocking - connection happens asynchronously.
     * 
     * @param config WiFi credentials and base URL
     * @return true if initialization started successfully
     */
    bool initialize(WifiConfig* config) override;

    /**
     * Check if WiFi is connected and HTTP client is ready.
     * @return true if ready to process requests
     */
    bool isConnected() override;

    /**
     * Queue an HTTP request for processing.
     * Requests are processed asynchronously in update().
     * 
     * @param request The request to queue
     * @return true if request was queued successfully
     */
    bool queueRequest(HttpRequest& request) override;

    /**
     * Process WiFi connection and queued HTTP requests.
     * Should be called regularly from the main loop.
     */
    void update() override;

    /**
     * Disconnect WiFi and cleanup resources.
     */
    void disconnect() override;

    /**
     * Update WiFi credentials and base URL.
     * Will trigger a reconnect if currently connected.
     * 
     * @param config New WiFi configuration
     */
    void updateConfig(WifiConfig* config) override;

    // Friend declaration for the event handler
    friend esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt);

private:
    // WiFi connection management
    void startWifiConnection();
    void checkWifiConnection();
    void logWifiStatusError();

    // HTTP client management
    bool initializeHttpClient();
    void cleanupHttpClient();

    // Request processing
    void processQueuedRequests();
    void initiateHttpRequest(HttpRequest& request);
    void checkOngoingRequests();
    void handleRequestError(HttpRequest& request, WirelessErrorInfo error);

    // Configuration
    WifiConfig* wifiConfig;
    
    // WiFi connection state
    bool wifiConnected;
    bool httpClientInitialized;
    int wifiConnectionAttempts;
    SimpleTimer connectionAttemptTimer;
    static const int MAX_WIFI_CONN_ATTEMPTS = 20;
    static const uint8_t MAX_RETRIES = 1;

    // HTTP client state
    uint8_t channel;
    std::queue<HttpRequest> httpQueue;
    esp_http_client_handle_t httpClient;
    HttpRequest* currentRequest;
};