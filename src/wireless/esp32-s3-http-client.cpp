#include "wireless/esp32-s3-http-client.hpp"
#include "logger.hpp"
#include <cmath>

static const char* TAG = "HttpClient";

esp_err_t esp32_http_event_handler(esp_http_client_event_t *evt) {
    Esp32S3HttpClient* client = static_cast<Esp32S3HttpClient*>(evt->user_data);
    HttpRequest* request = client->currentRequest;
    char error_msg[64] = {0};
    
    if (!request) {
        LOG_E(TAG, "HTTP event with no active request");
        return ESP_OK;
    }
    
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            LOG_E(TAG, "HTTP error for: %s", request->path.c_str());
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
                LOG_E(TAG, "HTTP %d for: %s", status_code, request->path.c_str());
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
                LOG_W(TAG, "Disconnected during request: %s", request->path.c_str());
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

Esp32S3HttpClient::Esp32S3HttpClient()
    : wifiConfig(nullptr)
    , wifiConnected(false)
    , httpClientInitialized(false)
    , wifiConnectionAttempts(0)
    , channel(0)
    , httpClient(nullptr)
    , currentRequest(nullptr) {
}

Esp32S3HttpClient::~Esp32S3HttpClient() {
    disconnect();
}

bool Esp32S3HttpClient::initialize(WifiConfig* config) {
    wifiConfig = config;
    wifiConnected = false;
    httpClientInitialized = false;
    wifiConnectionAttempts = 0;
    
    LOG_I(TAG, "Connecting to: %s", wifiConfig->ssid.c_str());
    startWifiConnection();
    connectionAttemptTimer.setTimer(500);
    
    return true;
}

bool Esp32S3HttpClient::isConnected() {
    return wifiConnected && httpClientInitialized;
}

bool Esp32S3HttpClient::queueRequest(HttpRequest& request) {
    httpQueue.push(request);
    return true;
}

void Esp32S3HttpClient::update() {
    if (!wifiConnected) {
        checkWifiConnection();
    } else if (!httpClientInitialized) {
        httpClientInitialized = initializeHttpClient();
    } else {
        processQueuedRequests();
        checkOngoingRequests();
    }
}

void Esp32S3HttpClient::disconnect() {
    cleanupHttpClient();
    WiFi.disconnect(true);
    
    wifiConnected = false;
    httpClientInitialized = false;
    wifiConnectionAttempts = 0;
    connectionAttemptTimer.invalidate();
}

void Esp32S3HttpClient::updateConfig(WifiConfig* config) {
    bool needsReconnect = wifiConnected && 
        (config->ssid != wifiConfig->ssid || config->password != wifiConfig->password);
    
    wifiConfig = config;
    
    if (needsReconnect) {
        disconnect();
        initialize(config);
    }
}

void Esp32S3HttpClient::startWifiConnection() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiConfig->ssid.c_str(), wifiConfig->password.c_str());
}

void Esp32S3HttpClient::checkWifiConnection() {
    connectionAttemptTimer.updateTime();
    
    if (!connectionAttemptTimer.expired()) {
        return;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        channel = WiFi.channel();
        LOG_I(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
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

void Esp32S3HttpClient::logWifiStatusError() {
    wl_status_t status = WiFi.status();
    const char* msg = "Unknown error";
    
    switch (status) {
        case WL_NO_SSID_AVAIL: msg = "SSID not found"; break;
        case WL_CONNECT_FAILED: msg = "Connection failed"; break;
        case WL_CONNECTION_LOST: msg = "Connection lost"; break;
        case WL_DISCONNECTED: msg = "Disconnected"; break;
        default: break;
    }
    
    LOG_E(TAG, "WiFi failed: %s (%s)", msg, wifiConfig->ssid.c_str());
}

bool Esp32S3HttpClient::initializeHttpClient() {
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
        LOG_E(TAG, "Failed to init HTTP client");
    }
    
    return httpClient != nullptr;
}

void Esp32S3HttpClient::cleanupHttpClient() {
    if (httpClient) {
        esp_http_client_cleanup(httpClient);
        httpClient = nullptr;
    }
}

void Esp32S3HttpClient::processQueuedRequests() {
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

void Esp32S3HttpClient::initiateHttpRequest(HttpRequest& request) {
    // Construct full URL
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
        LOG_E(TAG, "Request failed: %s", esp_err_to_name(err));
        handleRequestError(request, {
            WirelessError::CONNECTION_FAILED,
            esp_err_to_name(err),
            request.retryCount < MAX_RETRIES
        });
        cleanupHttpClient();
        initializeHttpClient();
    }
}

void Esp32S3HttpClient::checkOngoingRequests() {
    if (httpQueue.empty() || !httpQueue.front().inProgress) {
        return;
    }

    HttpRequest& request = httpQueue.front();
    unsigned long currentTime = SimpleTimer::getPlatformClock()->milliseconds();
    unsigned long elapsedTime = currentTime - request.lastAttemptTime;
    
    if (elapsedTime > 15000) {
        LOG_E(TAG, "Timeout: %s", request.path.c_str());
        
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

void Esp32S3HttpClient::handleRequestError(HttpRequest& request, WirelessErrorInfo error) {
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
