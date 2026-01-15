#pragma once

#include <WiFi.h>
#include <esp_http_client.h>
#include <queue>
#include "driver-interface.hpp"
#include "wireless-types.hpp"
#include "utils/simple-timer.hpp"

// Forward declaration for the event handler
class Esp32S3HttpClient;
static const char* HTTP_TAG = "HttpClient";

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
    static const int MAX_WIFI_CONN_ATTEMPTS = 20;
    static const uint8_t MAX_RETRIES = 1;

    Esp32S3HttpClient(std::string name, WifiConfig* wifiConfig)
        : HttpClientDriverInterface(name)
        , wifiConfig(wifiConfig)
        , wifiConnected(false)
        , httpClientInitialized(false)
        , wifiConnectionAttempts(0)
        , channel(0)
        , httpClient(nullptr)
        , currentRequest(nullptr) {
    }

    ~Esp32S3HttpClient() override {
        disconnect();
    }

    int initialize() override {
        wifiConnected = false;
        httpClientInitialized = false;
        wifiConnectionAttempts = 0;
        
        LOG_I(HTTP_TAG, "Connecting to: %s", wifiConfig->ssid.c_str());
        startWifiConnection();
        connectionAttemptTimer.setTimer(500);
        
        return 0;
    }

    void setWifiConfig(WifiConfig* config) override {
        wifiConfig = config;
        wifiConnected = false;
        httpClientInitialized = false;
        wifiConnectionAttempts = 0;
        
        LOG_I(HTTP_TAG, "Connecting to: %s", wifiConfig->ssid.c_str());
        startWifiConnection();
        connectionAttemptTimer.setTimer(500);
    }

    bool isConnected() override {
        return wifiConnected && httpClientInitialized;
    }

    bool queueRequest(HttpRequest& request) override {
        httpQueue.push(request);
        return true;
    }

    void exec() override {
        if (!wifiConnected) {
            checkWifiConnection();
        } else if (!httpClientInitialized) {
            httpClientInitialized = initializeHttpClient();
        } else {
            processQueuedRequests();
            checkOngoingRequests();
        }
    }

    void disconnect() override {
        cleanupHttpClient();
        WiFi.disconnect(true);
        
        wifiConnected = false;
        httpClientInitialized = false;
        wifiConnectionAttempts = 0;
        connectionAttemptTimer.invalidate();
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

    friend esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt);

private:
    void startWifiConnection() {
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiConfig->ssid.c_str(), wifiConfig->password.c_str());
    }

    void checkWifiConnection() {
        connectionAttemptTimer.updateTime();
        
        if (!connectionAttemptTimer.expired()) {
            return;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            channel = WiFi.channel();
            LOG_I(HTTP_TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
            wifiConnected = true;
            httpClientInitialized = initializeHttpClient();
        } else {
            wifiConnectionAttempts++;
            if (wifiConnectionAttempts < MAX_WIFI_CONN_ATTEMPTS) {
                connectionAttemptTimer.setTimer(500);
            } else {
                logWifiStatusError();
                wifiConnectionAttempts = 0;
                connectionAttemptTimer.setTimer(10000);
            }
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

    void handleRequestError(HttpRequest& request, WirelessErrorInfo error) {
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

    // Configuration
    WifiConfig* wifiConfig;
    
    // WiFi connection state
    bool wifiConnected;
    bool httpClientInitialized;
    int wifiConnectionAttempts;
    SimpleTimer connectionAttemptTimer;

    // HTTP client state
    uint8_t channel;
    std::queue<HttpRequest> httpQueue;
    esp_http_client_handle_t httpClient;
    HttpRequest* currentRequest;
};

// Event handler must be defined after the class
inline esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt) {
    Esp32S3HttpClient* client = static_cast<Esp32S3HttpClient*>(evt->user_data);
    HttpRequest* request = client->currentRequest;
    char error_msg[64] = {0};
    
    if (!request) {
        LOG_E(HTTP_TAG, "HTTP event with no active request");
        return ESP_OK;
    }
    
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            LOG_E(HTTP_TAG, "HTTP error for: %s", request->path.c_str());
            request->retryCount++;
            if (request->retryCount >= Esp32S3HttpClient::MAX_RETRIES && request->onError) {
                request->onError({WirelessError::CONNECTION_FAILED, "HTTP error - max retries", false});
            }
            break;
            
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len) {
                request->responseData.append(reinterpret_cast<char*>(evt->data), evt->data_len);
            }
            break;
            
        case HTTP_EVENT_ON_FINISH: {
            int status_code = esp_http_client_get_status_code(client->httpClient);
            
            if (status_code >= 200 && status_code < 300) {
                if (request->onSuccess) {
                    request->onSuccess(request->responseData);
                }
            } else {
                LOG_E(HTTP_TAG, "HTTP %d for: %s", status_code, request->path.c_str());
                if (request->onError) {
                    snprintf(error_msg, sizeof(error_msg), "HTTP Error: %d", status_code);
                    request->onError({
                        status_code >= 500 ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE,
                        error_msg,
                        false
                    });
                }
            }
            
            request->inProgress = false;
            client->currentRequest = nullptr;
            client->httpQueue.pop();
            break;
        }
            
        case HTTP_EVENT_DISCONNECTED:
            if (request->inProgress) {
                LOG_W(HTTP_TAG, "Disconnected during request: %s", request->path.c_str());
                request->inProgress = false;
                client->currentRequest = nullptr;
                
                if (request->retryCount >= Esp32S3HttpClient::MAX_RETRIES || !request->responseData.empty()) {
                    if (request->onError) {
                        request->onError({WirelessError::CONNECTION_FAILED, "Connection closed", false});
                    }
                    client->httpQueue.pop();
                } else {
                    request->retryCount++;
                }
            }
            break;
            
        default:
            break;
    }
    
    return ESP_OK;
}