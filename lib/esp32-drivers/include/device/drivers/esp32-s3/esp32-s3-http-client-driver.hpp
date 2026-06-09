#pragma once

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_http_client.h>
#include <queue>
#include "device/drivers/driver-interface.hpp"
#include "wireless/wireless-types.hpp"
#include "utils/simple-timer.hpp"

class Esp32S3HttpClient;
static const char* const HTTP_TAG = "HttpClient";

// Channel ESP-NOW falls back to when the WiFi connection fails. IMPORTANT:
// configure your WiFi AP to this same channel for reliable ESP-NOW.
static constexpr uint8_t ESPNOW_FALLBACK_CHANNEL = 6;

inline esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt);

// WiFi connection and HTTP requests over ESP-IDF's esp_http_client: connection
// monitoring, async request queueing, and retry with exponential backoff.
class Esp32S3HttpClient : public HttpClientDriverInterface {
public:
    static const int WIFI_CONNECTION_TIMEOUT_MS = 12500;
    static const uint8_t MAX_RETRIES = 1;

    Esp32S3HttpClient(const std::string& name, WifiConfig* config)
        : HttpClientDriverInterface(name)
        , wifiConfig(config) {
    }

    ~Esp32S3HttpClient() override {
        disconnect();
    }

    int initialize() override {
        return 0;
    }

    bool isConnected() override {
        return connectionState == ConnectionState::CONNECTED;
    }

    bool queueRequest(HttpRequest& request) override {
        httpQueue.push(request);
        return true;
    }

    void exec() override {
        switch (connectionState) {
            case ConnectionState::CONNECTING:
                checkWifiConnection();
                break;
            case ConnectionState::CONNECTED:
                processQueuedRequests();
                checkOngoingRequests();
                break;
            default:
                break;
        }
    }

    void disconnect() override {
        cleanupHttpClient();
        // disconnect() alone doesn't stop the core's reconnect scan loop, which
        // drags the radio off ESPNOW_CHANNEL.
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false);  // Disconnect from AP but keep WiFi radio on for ESP-NOW

        connectionAttemptTimer.invalidate();
        connectionState = ConnectionState::IDLE;
    }

    uint8_t* getMacAddress() override {
        esp_read_mac(macAddress_, ESP_MAC_WIFI_STA);
        return macAddress_;
    }

    void setHttpClientState(HttpClientState state) override {
        if (state == HttpClientState::WIFI_ENGAGED && connectionState == ConnectionState::IDLE) {
            connect();
        } else if (state == HttpClientState::IDLE && connectionState != ConnectionState::IDLE) {
            disconnect();
        }
    }

    HttpClientState getHttpClientState() override {
        return connectionState == ConnectionState::IDLE
            ? HttpClientState::IDLE
            : HttpClientState::WIFI_ENGAGED;
    }

    friend esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt);

private:
    enum class ConnectionState {
        IDLE,        // WiFi mode not requested
        CONNECTING,  // waiting for AP association, bounded by connectionAttemptTimer
        CONNECTED,   // AP up, HTTP requests flow
        FAILED       // gave up; radio parked on the ESP-NOW fallback channel
    };

    void connect() {
        startWifiConnection();
        connectionAttemptTimer.setTimer(WIFI_CONNECTION_TIMEOUT_MS);
        connectionState = ConnectionState::CONNECTING;
    }

    void startWifiConnection() {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false);  // Clear any previous connection but keep radio on
        WiFi.setAutoReconnect(true);  // Let WiFi stack handle reconnection attempts
        WiFi.channel(6);
        WiFi.begin(wifiConfig->ssid.c_str(), wifiConfig->password.c_str());
        LOG_I(HTTP_TAG, "Starting WiFi connection, timeout: %dms", WIFI_CONNECTION_TIMEOUT_MS);
    }

    void checkWifiConnection() {
        connectionAttemptTimer.updateTime();

        if (WiFi.status() == WL_CONNECTED) {
            LOG_I(HTTP_TAG, "WiFi connected, IP: %s, Channel: %d", WiFi.localIP().toString().c_str(), WiFi.channel());
            connectionAttemptTimer.invalidate();
            // On init failure httpClient stays null; initiateHttpRequest retries
            // per request attempt.
            initializeHttpClient();
            connectionState = ConnectionState::CONNECTED;
            // Leave auto-reconnect ON so WiFi recovers from disconnections
            return;
        }

        if (connectionAttemptTimer.expired()) {
            logWifiStatusError();
            LOG_W(HTTP_TAG, "WiFi connection timeout after %dms. Device will work offline with ESP-NOW only.", WIFI_CONNECTION_TIMEOUT_MS);
            WiFi.setAutoReconnect(false);  // Stop the retry spam
            WiFi.disconnect(false);  // Drop the AP but keep the radio on for ESP-NOW

            // Force fallback channel; AP must match it for ESP-NOW compatibility.
            WiFi.channel(ESPNOW_FALLBACK_CHANNEL);
            LOG_I(HTTP_TAG, "Set fallback WiFi channel to %d for ESP-NOW", ESPNOW_FALLBACK_CHANNEL);

            connectionAttemptTimer.invalidate();
            connectionState = ConnectionState::FAILED;
        }
    }

    void logWifiStatusError() {
        wl_status_t status = WiFi.status();
        const char* msg = "Unknown error";
        
        switch (status) {
            case WL_NO_SSID_AVAIL: msg = "SSID not found"; break;
            case WL_CONNECT_FAILED: msg = "Connection failed"; break;
            case WL_CONNECTION_LOST: msg = "Connection lost"; break;
            case WL_DISCONNECTED: msg = "Disconnected"; break;
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
        
        if (WiFi.status() != WL_CONNECTED) {
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
            unsigned long backoffTime = pow(2, request.retryCount) * 1000;
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

    WifiConfig* wifiConfig = nullptr;
    uint8_t macAddress_[6];

    ConnectionState connectionState = ConnectionState::IDLE;
    SimpleTimer connectionAttemptTimer;

    std::queue<HttpRequest> httpQueue;
    esp_http_client_handle_t httpClient = nullptr;
    HttpRequest* currentRequest = nullptr;
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
