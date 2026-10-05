#pragma once

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_mac.h>
#include <esp_http_client.h>
#include <queue>
#include "device/drivers/driver-interface.hpp"
#include "wireless/wireless-types.hpp"
#include "utils/simple-timer.hpp"

// Forward declaration for the event handler
class Esp32S3HttpClient;
static const char* const HTTP_TAG = "HttpClient";

// Fallback channel for ESP-NOW when WiFi connection fails
// IMPORTANT: Configure your WiFi AP to use this same channel for reliable ESP-NOW!
static constexpr uint8_t ESPNOW_FALLBACK_CHANNEL = 6;

inline esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt);

/**
 * Handles WiFi connection and HTTP requests using ESP-IDF's esp_http_client.
 * 
 * This class manages:
 * - WiFi connection establishment and monitoring
 * - HTTP client initialization and cleanup
 * - Asynchronous request queueing and processing
 * - Retry logic with exponential backoff
 */
class Esp32S3HttpClient : public HttpClientDriverInterface {
public:
    static const int CONNECT_POLL_MS = 100;
    static const int WIFI_CONNECTION_TIMEOUT_MS = 12500;  // 2.5 sec * 5 attempts
    static const uint8_t MAX_RETRIES = 1;

    Esp32S3HttpClient(const std::string& name, WifiConfig* config)
        : HttpClientDriverInterface(name)
        , wifiConfig(config) {
    }

    ~Esp32S3HttpClient() override {
        disconnect();
    }

    int initialize() override {
        wifiConnected = false;
        httpClientInitialized = false;
        wifiGivenUp = false;
        
        return 0;
    }

    void setWifiConfig(WifiConfig* config) override {
        wifiConfig = config;
        wifiConnected = false;
        httpClientInitialized = false;
        wifiGivenUp = false;
        
        LOG_I(HTTP_TAG, "Connecting to: %s", wifiConfig->ssid.c_str());
        startWifiConnection();
        connectionAttemptTimer.setTimer(WIFI_CONNECTION_TIMEOUT_MS);
    }

    bool isConnected() override {
        return wifiConnected && httpClientInitialized;
    }

    bool queueRequest(HttpRequest& request) override {
        httpQueue.push(request);
        return true;
    }

    void exec() override {
        if (!wifiConnected && !wifiGivenUp) {
            checkWifiConnection();
        } else if (wifiConnected && !httpClientInitialized) {
            httpClientInitialized = initializeHttpClient();
        } else if (wifiConnected) {
            processQueuedRequests();
            checkOngoingRequests();
        }
    }

    void disconnect() override {
        cleanupHttpClient();
        autoReconnect.store(false);
        esp_wifi_disconnect();  // Leave the AP but keep the radio on for ESP-NOW
        
        wifiConnected = false;
        httpClientInitialized = false;
        wifiGivenUp = false;
        connectionAttemptTimer.invalidate();
        httpClientState = HttpClientState::DISCONNECTED;
    }

    void updateConfig(WifiConfig* config) override {
        bool needsReconnect = wifiConnected && 
            (config->ssid != wifiConfig->ssid || config->password != wifiConfig->password);
        
        wifiConfig = config;
        
        if (needsReconnect) {
            disconnect();
            setWifiConfig(config);
        }
    }

    void retryConnection() override {
        if (!wifiConnected) {
            LOG_I(HTTP_TAG, "Retrying WiFi connection...");
            wifiGivenUp = false;
            startWifiConnection();
            connectionAttemptTimer.setTimer(WIFI_CONNECTION_TIMEOUT_MS);
        }
    }

    uint8_t* getMacAddress() override {
        esp_read_mac(macAddress_, ESP_MAC_WIFI_STA);
        return macAddress_;
    }

    void setHttpClientState(HttpClientState state) override {
        if(state == HttpClientState::CONNECTED && httpClientState != HttpClientState::CONNECTED) {
            connect();
        }
        else if(state == HttpClientState::DISCONNECTED && httpClientState != HttpClientState::DISCONNECTED) {
            disconnect();
        }
        httpClientState = state;
    }

    HttpClientState getHttpClientState() override {
        return httpClientState;
    }

    void connect() {
        LOG_I(HTTP_TAG, "Enabling HTTP mode...");
        
        if (wifiConnected && isStationConnected()) {
            LOG_D(HTTP_TAG, "Already connected to WiFi");
        }
        
        // Start WiFi connection
        startWifiConnection();
        
        // Wait for connection with timeout
        int attempts = 0;
        const int maxAttempts = 50;  // 5 seconds max (50 * 100ms)
        while (!isStationConnected() && attempts < maxAttempts) {
            vTaskDelay(pdMS_TO_TICKS(CONNECT_POLL_MS));
            attempts++;
        }
        
        if (isStationConnected()) {
            wifiConnected = true;
            wifiGivenUp = false;
            channel = stationChannel();
            LOG_I(HTTP_TAG, "WiFi connected, IP: %s, Channel: %d", 
                  stationIpString().c_str(), channel);
            
            if (!httpClientInitialized) {
                httpClientInitialized = initializeHttpClient();
            }
            httpClientState = HttpClientState::CONNECTED;
        } else {
            LOG_W(HTTP_TAG, "WiFi connection failed after %d attempts", attempts);
        }
    }

    friend esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt);

private:
    /**
     * Station states this driver distinguishes. Arduino's wl_status_t was a
     * polled value backed by its own event handlers; IDF only delivers events,
     * so the equivalent is tracked here.
     */
    enum class StationStatus { DISCONNECTED, NO_SSID_AVAIL, CONNECT_FAILED, CONNECTION_LOST, CONNECTED };

    // Written from the WiFi event task and read from exec() on the main loop.
    static inline std::atomic<StationStatus> stationStatus{StationStatus::DISCONNECTED};
    static inline std::atomic<uint32_t> stationIp{0};
    // Arduino's setAutoReconnect(true) retried on its own. IDF does not, so the
    // disconnect handler consults this and re-dials.
    static inline std::atomic<bool> autoReconnect{false};
    static inline bool wifiStackReady = false;

    static void onWifiEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
        (void)arg;
        if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            // Arduino reported WL_CONNECTED only once an IP existed, not on
            // association, so this is the event that means connected.
            auto* event = static_cast<ip_event_got_ip_t*>(data);
            stationIp.store(event->ip_info.ip.addr);
            stationStatus.store(StationStatus::CONNECTED);
            return;
        }
        if (base != WIFI_EVENT || id != WIFI_EVENT_STA_DISCONNECTED) {
            return;
        }
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(data);
        bool wasConnected = stationStatus.load() == StationStatus::CONNECTED;
        stationIp.store(0);
        switch (event->reason) {
            case WIFI_REASON_NO_AP_FOUND:
                stationStatus.store(StationStatus::NO_SSID_AVAIL);
                break;
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_ASSOC_FAIL:
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                stationStatus.store(StationStatus::CONNECT_FAILED);
                break;
            default:
                stationStatus.store(wasConnected ? StationStatus::CONNECTION_LOST
                                                 : StationStatus::DISCONNECTED);
                break;
        }
        if (autoReconnect.load()) {
            esp_wifi_connect();
        }
    }

    /**
     * Brings up everything a station needs: the netif and event loop, the radio
     * if nothing has started it, a default STA netif for DHCP, and the two event
     * handlers. The ESP-NOW driver raises the same radio and creates the same
     * netif, so those two steps are individually guarded and no-op on the second
     * caller; the event handlers are registered only here, so this body still
     * runs in full even when ESP-NOW got there first.
     */
    void ensureWifiStack() {
        if (wifiStackReady) {
            return;
        }
        esp_netif_init();
        esp_event_loop_create_default();
        wifi_mode_t mode;
        if (esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT) {
            wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
            if (esp_wifi_init(&init) != ESP_OK) {
                LOG_E(HTTP_TAG, "esp_wifi_init failed");
                return;
            }
            esp_wifi_set_storage(WIFI_STORAGE_RAM);
        }
        if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
            esp_netif_create_default_wifi_sta();
        }
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr, nullptr);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onWifiEvent, nullptr, nullptr);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        wifiStackReady = true;
    }

    static bool isStationConnected() {
        return stationStatus.load() == StationStatus::CONNECTED;
    }

    static int stationChannel() {
        uint8_t primary = 0;
        wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
        esp_wifi_get_channel(&primary, &secondary);
        return primary;
    }

    static std::string stationIpString() {
        uint32_t addr = stationIp.load();
        char text[16] = {};
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u",
                      static_cast<unsigned>(addr & 0xFF), static_cast<unsigned>((addr >> 8) & 0xFF),
                      static_cast<unsigned>((addr >> 16) & 0xFF), static_cast<unsigned>((addr >> 24) & 0xFF));
        return text;
    }

    void startWifiConnection() {
        ensureWifiStack();
        autoReconnect.store(true);
        esp_wifi_disconnect();
        // WiFi.channel(6) before begin. The AP's own channel wins once
        // associated; this only pins where the radio sits until then.
        esp_wifi_set_channel(ESPNOW_FALLBACK_CHANNEL, WIFI_SECOND_CHAN_NONE);

        wifi_config_t config = {};
        // WiFi.begin set both of these and a zero-initialised config does not:
        // without pmf.capable an AP that requires management-frame protection
        // refuses the association, and an authmode floor of OPEN would also join
        // an open AP broadcasting our SSID.
        config.sta.pmf_cfg.capable = true;
        config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        // memcpy rather than snprintf: a 32-character SSID or a 64-character key
        // fills the field exactly, leaving no room for a terminator to be written.
        std::memcpy(config.sta.ssid, wifiConfig->ssid.data(),
                    std::min(wifiConfig->ssid.size(), sizeof(config.sta.ssid)));
        std::memcpy(config.sta.password, wifiConfig->password.data(),
                    std::min(wifiConfig->password.size(), sizeof(config.sta.password)));
        esp_wifi_set_config(WIFI_IF_STA, &config);

        stationStatus.store(StationStatus::DISCONNECTED);
        esp_wifi_connect();
        LOG_I(HTTP_TAG, "Starting WiFi connection, timeout: %dms", WIFI_CONNECTION_TIMEOUT_MS);
    }

    void checkWifiConnection() {
        connectionAttemptTimer.updateTime();
        
        // Check if we've connected
        if (isStationConnected()) {
            channel = stationChannel();
            LOG_I(HTTP_TAG, "WiFi connected, IP: %s, Channel: %d", stationIpString().c_str(), channel);
            wifiConnected = true;
            wifiGivenUp = false;
            connectionAttemptTimer.invalidate();
            httpClientInitialized = initializeHttpClient();
            // Leave auto-reconnect ON so WiFi recovers from disconnections
            return;
        }
        
        // Check if connection timeout expired
        if (connectionAttemptTimer.expired()) {
            logWifiStatusError();
            wifiGivenUp = true;
            LOG_W(HTTP_TAG, "WiFi connection timeout after %dms. Device will work offline with ESP-NOW only.", WIFI_CONNECTION_TIMEOUT_MS);
            autoReconnect.store(false);  // Stop the retry spam
            esp_wifi_disconnect();  // Leave the AP but keep the radio on for ESP-NOW
            
            // Force fallback channel for ESP-NOW compatibility
            // IMPORTANT: Configure your WiFi AP to use this same channel!
            esp_wifi_set_channel(ESPNOW_FALLBACK_CHANNEL, WIFI_SECOND_CHAN_NONE);
            LOG_I(HTTP_TAG, "Set fallback WiFi channel to %d for ESP-NOW", ESPNOW_FALLBACK_CHANNEL);
            
            connectionAttemptTimer.invalidate();
        }
    }

    void logWifiStatusError() {
        const char* msg = "Unknown error";
        
        switch (stationStatus.load()) {
            case StationStatus::NO_SSID_AVAIL:  msg = "SSID not found";    break;
            case StationStatus::CONNECT_FAILED: msg = "Connection failed"; break;
            case StationStatus::CONNECTION_LOST: msg = "Connection lost";  break;
            case StationStatus::DISCONNECTED:   msg = "Disconnected";      break;
            default: break;
        }
        
        LOG_E(HTTP_TAG, "WiFi failed: %s (%s)", msg, wifiConfig->ssid.c_str());
    }

    bool initializeHttpClient() {
        esp_http_client_config_t config = {};
        config.event_handler = esp32_http_event_handler;
        config.timeout_ms = 10000;
        config.user_data = this;
        config.keep_alive_enable = true;
        config.url = wifiConfig->baseUrl.c_str();
        config.skip_cert_common_name_check = true;
        config.cert_pem = nullptr;
        config.is_async = true;
        
        httpClient = esp_http_client_init(&config);
        
        if (!httpClient) {
            LOG_E(HTTP_TAG, "Failed to init HTTP client");
        }
        
        return httpClient != nullptr;
    }

    void cleanupHttpClient() {
        if (httpClient) {
            esp_http_client_cleanup(httpClient);
            httpClient = nullptr;
        }
    }

    void processQueuedRequests() {
        if (httpQueue.empty() || httpQueue.front().inProgress) {
            return;
        }

        HttpRequest& request = httpQueue.front();
        unsigned long currentTime = SimpleTimer::getPlatformClock()->milliseconds();
        unsigned long timeSinceLastAttempt = 
            request.lastAttemptTime > 0 ? currentTime - request.lastAttemptTime : ULONG_MAX;
            
        if (timeSinceLastAttempt > 1000 || request.lastAttemptTime == 0) {
            initiateHttpRequest(request);
        }
    }

    void initiateHttpRequest(HttpRequest& request) {
        std::string fullUrl = wifiConfig->baseUrl;
        
        if (fullUrl.find("http://") == std::string::npos && 
            fullUrl.find("https://") == std::string::npos) {
            fullUrl = "http://" + fullUrl;
        }
        
        if (!fullUrl.empty() && !request.path.empty()) {
            if (fullUrl.back() == '/' && request.path.front() == '/') {
                fullUrl += request.path.substr(1);
            } else if (fullUrl.back() != '/' && request.path.front() != '/') {
                fullUrl += "/" + request.path;
            } else {
                fullUrl += request.path;
            }
        }
        
        if (!isStationConnected()) {
            handleRequestError(request, {WirelessError::WIFI_NOT_CONNECTED, "WiFi lost", false});
            return;
        }

        if (!httpClient && !initializeHttpClient()) {
            handleRequestError(request, {WirelessError::CONNECTION_FAILED, "HTTP init failed", false});
            return;
        }

        request.responseData = "";
        request.inProgress = true;
        request.lastAttemptTime = SimpleTimer::getPlatformClock()->milliseconds();
        currentRequest = &request;

        esp_http_client_set_url(httpClient, fullUrl.c_str());
        
        if (request.method == "POST") {
            esp_http_client_set_method(httpClient, HTTP_METHOD_POST);
        } else if (request.method == "PUT") {
            esp_http_client_set_method(httpClient, HTTP_METHOD_PUT);
        } else {
            esp_http_client_set_method(httpClient, HTTP_METHOD_GET);
        }

        if (request.method == "POST" || request.method == "PUT") {
            esp_http_client_set_header(httpClient, "Content-Type", "application/json");
            esp_http_client_set_post_field(httpClient, request.payload.c_str(), request.payload.length());
        }

        esp_err_t err = esp_http_client_perform(httpClient);
        
        if (err != ESP_OK) {
            LOG_E(HTTP_TAG, "Request failed: %s", esp_err_to_name(err));
            handleRequestError(request, {
                WirelessError::CONNECTION_FAILED,
                esp_err_to_name(err),
                request.retryCount < MAX_RETRIES
            });
            cleanupHttpClient();
            initializeHttpClient();
        }
    }

    void checkOngoingRequests() {
        if (httpQueue.empty() || !httpQueue.front().inProgress) {
            return;
        }

        HttpRequest& request = httpQueue.front();
        unsigned long currentTime = SimpleTimer::getPlatformClock()->milliseconds();
        unsigned long elapsedTime = currentTime - request.lastAttemptTime;
        
        if (elapsedTime > 15000) {
            LOG_E(HTTP_TAG, "Timeout: %s", request.path.c_str());
            
            request.retryCount++;
            request.inProgress = false;
            currentRequest = nullptr;
            
            if (request.retryCount >= MAX_RETRIES) {
                if (request.onError) {
                    request.onError({WirelessError::TIMEOUT, "Request timed out", false});
                }
                httpQueue.pop();
            }
            
            cleanupHttpClient();
            initializeHttpClient();
        }
    }

    void handleRequestError(HttpRequest& request, const WirelessErrorInfo& error) {
        if (request.onError) {
            request.onError(error);
        }
        
        request.inProgress = false;
        currentRequest = nullptr;
        
        if (error.code != WirelessError::CONNECTION_FAILED || request.retryCount >= MAX_RETRIES) {
            httpQueue.pop();
        } else {
            request.retryCount++;
            unsigned long backoffTime = (1UL << request.retryCount) * 1000;
            request.lastAttemptTime = SimpleTimer::getPlatformClock()->milliseconds() + backoffTime - 1000;
        }
    }

    // HTTP event handlers - called from esp32_http_event_handler
    void handleHttpError(HttpRequest* request) {
        LOG_E(HTTP_TAG, "HTTP error for: %s", request->path.c_str());
        request->retryCount++;
        if (request->retryCount >= MAX_RETRIES && request->onError) {
            request->onError({WirelessError::CONNECTION_FAILED, "HTTP error - max retries", false});
        }
    }

    void handleHttpData(HttpRequest* request, void* data, int dataLen) {
        if (dataLen > 0) {
            request->responseData.append(reinterpret_cast<char*>(data), dataLen);
        }
    }

    void handleHttpFinish(HttpRequest* request, int statusCode) {
        if (statusCode >= 200 && statusCode < 300) {
            if (request->onSuccess) {
                request->onSuccess(request->responseData);
            }
        } else {
            handleHttpStatusError(request, statusCode);
        }
        
        finalizeRequest(request);
    }

    void handleHttpStatusError(HttpRequest* request, int statusCode) {
        LOG_E(HTTP_TAG, "HTTP %d for: %s", statusCode, request->path.c_str());
        if (request->onError) {
            char errorMsg[64] = {0};
            snprintf(errorMsg, sizeof(errorMsg), "HTTP Error: %d", statusCode);
            WirelessError errorType = (statusCode >= 500) 
                ? WirelessError::SERVER_ERROR 
                : WirelessError::INVALID_RESPONSE;
            request->onError({errorType, errorMsg, false});
        }
    }

    void handleHttpDisconnect(HttpRequest* request) {
        if (!request->inProgress) {
            return;
        }
        
        LOG_W(HTTP_TAG, "Disconnected during request: %s", request->path.c_str());
        request->inProgress = false;
        currentRequest = nullptr;
        
        bool shouldRemoveFromQueue = (request->retryCount >= MAX_RETRIES) || 
                                      !request->responseData.empty();
        
        if (shouldRemoveFromQueue) {
            if (request->onError) {
                request->onError({WirelessError::CONNECTION_FAILED, "Connection closed", false});
            }
            httpQueue.pop();
        } else {
            request->retryCount++;
        }
    }

    void finalizeRequest(HttpRequest* request) {
        request->inProgress = false;
        currentRequest = nullptr;
        httpQueue.pop();
    }

    // Configuration
    WifiConfig* wifiConfig = nullptr;
    uint8_t macAddress_[6];
    
    // WiFi connection state
    bool wifiConnected = false;
    bool httpClientInitialized = false;
    bool wifiGivenUp = false;
    SimpleTimer connectionAttemptTimer;

    // HTTP client state
    uint8_t channel = 0;
    std::queue<HttpRequest> httpQueue;
    esp_http_client_handle_t httpClient = nullptr;
    HttpRequest* currentRequest = nullptr;
    HttpClientState httpClientState = HttpClientState::DISCONNECTED;
};

// Event handler must be defined after the class
inline esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt) {
    auto* client = static_cast<Esp32S3HttpClient*>(evt->user_data);
    auto* request = client->currentRequest;
    
    if (!request) {
        LOG_E(HTTP_TAG, "HTTP event with no active request");
        return ESP_OK;
    }
    
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            client->handleHttpError(request);
            break;
            
        case HTTP_EVENT_ON_DATA:
            client->handleHttpData(request, evt->data, evt->data_len);
            break;
            
        case HTTP_EVENT_ON_FINISH:
            client->handleHttpFinish(request, esp_http_client_get_status_code(client->httpClient));
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            client->handleHttpDisconnect(request);
            break;
            
        default:
            break;
    }
    
    return ESP_OK;
}
