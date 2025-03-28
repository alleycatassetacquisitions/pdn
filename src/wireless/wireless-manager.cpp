#include "wireless/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "device.hpp"
#include <esp_log.h>
#include <memory>
#include <utility>
#include <esp_http_client.h>

/**
 * Event handler for ESP-IDF HTTP client events.
 * This function processes various events during the HTTP request lifecycle
 * and manages the response data accumulation.
 * 
 * @param evt Pointer to the event structure containing event type and data
 * @return ESP_OK if event was handled successfully
 * 
 * Events handled:
 * - HTTP_EVENT_ERROR: Connection or protocol errors
 * - HTTP_EVENT_ON_DATA: Received response data chunks
 * - HTTP_EVENT_ON_FINISH: Request completed successfully
 * - HTTP_EVENT_DISCONNECTED: Connection terminated
 */
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    WifiState* state = static_cast<WifiState*>(evt->user_data);
    HttpRequest* request = state->currentRequest;
    char sample[51] = {0};
    char error_msg[64] = {0};
    int status_code = 0;
    
    if (!request) {
        ESP_LOGE("WirelessManager", "HTTP event with no active request! Event ID: %d", evt->event_id);
        return ESP_OK;
    }
    
    if (evt->event_id == HTTP_EVENT_ERROR) {
        ESP_LOGE("WirelessManager", "HTTP_EVENT_ERROR for path: %s", request->path.c_str());
        ESP_LOGE("WirelessManager", "Connection failed or protocol error occurred");
        
        request->retryCount++;
        ESP_LOGI("WirelessManager", "Request retry count: %d", request->retryCount);
        
        if (request->retryCount >= 3) {
            ESP_LOGE("WirelessManager", "Max retries reached for path: %s", request->path.c_str());
            if (request->hasErrorCallback) {
                request->onError({
                    WirelessError::CONNECTION_FAILED,
                    "HTTP Event Error - Max retries reached"
                });
            }
        } else {
            ESP_LOGI("WirelessManager", "Will retry request later");
        }
    } else if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        ESP_LOGI("WirelessManager", "HTTP_EVENT_ON_CONNECTED to: %s", request->path.c_str());
        ESP_LOGI("WirelessManager", "Connection established successfully");
    } else if (evt->event_id == HTTP_EVENT_HEADERS_SENT) {
        ESP_LOGI("WirelessManager", "HTTP_EVENT_HEADERS_SENT for: %s", request->path.c_str());
        ESP_LOGI("WirelessManager", "Request headers sent, waiting for response");
    } else if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        // Only log essential headers at INFO level
        if (strcmp(evt->header_key, "Content-Type") == 0 || 
            strcmp(evt->header_key, "Content-Length") == 0 ||
            strcmp(evt->header_key, "Status") == 0) {
            ESP_LOGI("WirelessManager", "Received header: %s: %s", 
                     evt->header_key, evt->header_value);
        } else {
            // Log other headers at DEBUG level only
            ESP_LOGD("WirelessManager", "Header: %s: %s", 
                     evt->header_key, evt->header_value);
        }
    } else if (evt->event_id == HTTP_EVENT_ON_DATA) {
        ESP_LOGI("WirelessManager", "HTTP_EVENT_ON_DATA for: %s, len=%d, data_len=%d", 
                 request->path.c_str(), evt->data_len, request->responseData.length());
        
        if (evt->data_len) {
            request->responseData.concat(String(reinterpret_cast<char*>(evt->data), evt->data_len));
            ESP_LOGI("WirelessManager", "Response data length now: %d", request->responseData.length());
            
            if (evt->data_len > 0) {
                strncpy(sample, reinterpret_cast<char*>(evt->data), evt->data_len > 50 ? 50 : evt->data_len);
                ESP_LOGI("WirelessManager", "Data sample: %s", sample);
            }
        } else {
            ESP_LOGW("WirelessManager", "Received empty data chunk");
        }
    } else if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        ESP_LOGI("WirelessManager", "HTTP_EVENT_ON_FINISH for: %s, response length: %d", 
                 request->path.c_str(), request->responseData.length());
        
        status_code = esp_http_client_get_status_code(state->httpClient);
        ESP_LOGI("WirelessManager", "HTTP status code: %d", status_code);
        
        if (status_code >= 200 && status_code < 300) {
            ESP_LOGI("WirelessManager", "Request successful with status %d", status_code);
            if (request->onSuccess) {
                ESP_LOGI("WirelessManager", "Calling success callback for: %s", request->path.c_str());
                request->onSuccess(request->responseData);
            }
        } else {
            ESP_LOGE("WirelessManager", "Request failed with status %d", status_code);
            if (request->hasErrorCallback) {
                snprintf(error_msg, sizeof(error_msg), "HTTP Error: %d", status_code);
                request->onError({
                    status_code >= 500 ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE,
                    error_msg
                });
            }
        }
    } else if (evt->event_id == HTTP_EVENT_DISCONNECTED) {
        ESP_LOGI("WirelessManager", "HTTP_EVENT_DISCONNECTED from: %s", request->path.c_str());
        ESP_LOGI("WirelessManager", "Connection closed");
    } else {
        ESP_LOGI("WirelessManager", "Unknown HTTP event: %d for: %s", 
                 evt->event_id, request->path.c_str());
    }
    
    return ESP_OK;
}

PowerOffState::PowerOffState() : WirelessState(WirelessStateId::POWER_OFF) {}

EspNowState::EspNowState() : WirelessState(WirelessStateId::ESP_NOW) {}

WifiState::WifiState(const char* ssid, const char* password, const char* baseUrl, std::queue<HttpRequest>* requestQueue) 
    : WirelessState(WirelessStateId::WIFI)
    , ssid(ssid)
    , password(password)
    , baseUrl(baseUrl)
    , httpQueue(requestQueue)
    , httpClient(nullptr)
    , currentRequest(nullptr) {
    ESP_LOGI("WirelessManager", "WifiState created with base URL: %s", baseUrl);
}

// PowerOffState Implementation
void PowerOffState::onStateMounted(Device* device) {
    powerDown();
}

void PowerOffState::onStateDismounted(Device* device) {
    // Nothing to clean up
}

void PowerOffState::powerDown() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    // TODO: ESP-NOW cleanup will be handled later
}

// EspNowState Implementation
void EspNowState::onStateMounted(Device* device) {
    ESP_LOGW("WirelessManager", "ESP-NOW integration pending");
    idleTimer.setTimer(IDLE_TIMEOUT);
}

void EspNowState::onStateLoop(Device* device) {
    idleTimer.updateTime();
}

void EspNowState::onStateDismounted(Device* device) {
    // TODO: ESP-NOW cleanup will be handled later
}

// WifiState Implementation
void WifiState::onStateMounted(Device* device) {
    idleTimer.setTimer(IDLE_TIMEOUT);
    if (initializeWiFi() && initializeHttpClient()) {
        ESP_LOGI("WirelessManager", "WiFi and HTTP client initialized successfully");
    } else {
        ESP_LOGE("WirelessManager", "Failed to initialize WiFi or HTTP client");
    }
}

void WifiState::onStateLoop(Device* device) {
    idleTimer.updateTime();
    processQueuedRequests();
    checkOngoingRequests();
}

void WifiState::onStateDismounted(Device* device) {
    cleanupHttpClient();
    WiFi.disconnect(true);
}

bool WifiState::initializeWiFi() {
    ESP_LOGI("WirelessManager", "Initializing WiFi with SSID: %s", ssid);
    
    // Explicitly set WiFi mode to station
    WiFi.mode(WIFI_STA);
    ESP_LOGI("WirelessManager", "WiFi mode set to STATION");
    
    // Start connection attempt
    ESP_LOGI("WirelessManager", "Starting WiFi connection attempt...");
    WiFi.begin(ssid, password);
    
    // Log connection attempts
    int attempts = 0;
    const int maxAttempts = 20;
    ESP_LOGI("WirelessManager", "Waiting for connection (max %d attempts)...", maxAttempts);
    
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        attempts++;
        ESP_LOGI("WirelessManager", "Connection attempt %d/%d, status: %d", 
                 attempts, maxAttempts, WiFi.status());
    }
    
    // Check connection result
    if (WiFi.status() == WL_CONNECTED) {
        channel = WiFi.channel();
        ESP_LOGI("WirelessManager", "WiFi connected successfully!");
        ESP_LOGI("WirelessManager", "IP address: %s", WiFi.localIP().toString().c_str());
        ESP_LOGI("WirelessManager", "Channel: %d, RSSI: %d dBm", channel, WiFi.RSSI());
        return true;
    } else {
        // Detailed error reporting based on status
        ESP_LOGE("WirelessManager", "Failed to connect to WiFi after %d attempts", attempts);
        
        // Detailed error reporting based on status
        wl_status_t status = WiFi.status();
        if (status == WL_NO_SHIELD) {
            ESP_LOGE("WirelessManager", "WiFi shield not present");
        } else if (status == WL_IDLE_STATUS) {
            ESP_LOGE("WirelessManager", "WiFi is in idle status, still trying");
        } else if (status == WL_NO_SSID_AVAIL) {
            ESP_LOGE("WirelessManager", "SSID not available: %s", ssid);
        } else if (status == WL_SCAN_COMPLETED) {
            ESP_LOGE("WirelessManager", "WiFi scan completed but no connection established");
        } else if (status == WL_CONNECT_FAILED) {
            ESP_LOGE("WirelessManager", "WiFi connection failed - check password");
        } else if (status == WL_CONNECTION_LOST) {
            ESP_LOGE("WirelessManager", "WiFi connection was lost");
        } else if (status == WL_DISCONNECTED) {
            ESP_LOGE("WirelessManager", "WiFi disconnected");
        } else {
            ESP_LOGE("WirelessManager", "Unknown WiFi status: %d", status);
        }
        
        return false;
    }
}

bool WifiState::initializeHttpClient() {
    ESP_LOGI("WirelessManager", "Initializing HTTP client with base URL: %s", baseUrl);
    
    esp_http_client_config_t config = {};
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;  // Increased timeout for better reliability
    config.user_data = this;  // Pass WifiState instance to event handler
    config.keep_alive_enable = true;  // Enable connection reuse
    config.url = baseUrl;  // Set the base URL
    config.skip_cert_common_name_check = true;  // Skip certificate CN validation
    config.cert_pem = nullptr;  // Skip certificate verification entirely
    
    ESP_LOGI("WirelessManager", "HTTP client config: timeout=%d ms, keep-alive=%s, base_url=%s, cert_verify=disabled", 
             config.timeout_ms, config.keep_alive_enable ? "enabled" : "disabled", config.url);
    
    httpClient = esp_http_client_init(&config);
    
    if (httpClient != nullptr) {
        ESP_LOGI("WirelessManager", "HTTP client initialized successfully");
        return true;
    } else {
        ESP_LOGE("WirelessManager", "Failed to initialize HTTP client");
        return false;
    }
}

void WifiState::cleanupHttpClient() {
    ESP_LOGI("WirelessManager", "Cleaning up HTTP client resources");
    
    if (httpClient) {
        esp_err_t err = esp_http_client_cleanup(httpClient);
        if (err == ESP_OK) {
            ESP_LOGI("WirelessManager", "HTTP client cleanup successful");
        } else {
            ESP_LOGE("WirelessManager", "HTTP client cleanup failed with error: %s", esp_err_to_name(err));
        }
        httpClient = nullptr;
    } else {
        ESP_LOGW("WirelessManager", "No HTTP client to clean up");
    }
}

/**
 * Processes queued HTTP requests and initiates new ones when ready.
 * This method is called periodically from the WiFi state loop.
 * 
 * @return true if queue processing completed successfully
 */
bool WifiState::processQueuedRequests() {
    static unsigned long lastQueueLogTime = 0;
    unsigned long currentTime = millis();
    
    // Log queue status every 5 seconds
    if (currentTime - lastQueueLogTime > 5000) {
        if (httpQueue->empty()) {
            ESP_LOGI("WirelessManager", "HTTP queue is empty");
        } else {
            ESP_LOGI("WirelessManager", "HTTP queue has %d pending requests", httpQueue->size());
        }
        lastQueueLogTime = currentTime;
    }
    
    // If queue is empty, nothing to do
    if (httpQueue->empty()) {
        return true;
    }

    // Get the front request
    HttpRequest& request = httpQueue->front();
    
    // Log request status - Changed from DEBUG to VERBOSE level to reduce spam
    if (request.inProgress) {
        // ESP_LOGV("WirelessManager", "Front request to %s is already in progress", 
        //          request.path.c_str());
    } else {
        // Check if we should initiate the request now
        unsigned long timeSinceLastAttempt = 
            request.lastAttemptTime > 0 ? currentTime - request.lastAttemptTime : ULONG_MAX;
            
        if (timeSinceLastAttempt > 1000 || request.lastAttemptTime == 0) {
            ESP_LOGI("WirelessManager", "Initiating request to %s (method: %s)", 
                     request.path.c_str(), request.method.c_str());
            
            // If this is a retry, log retry count
            if (request.retryCount > 0) {
                ESP_LOGI("WirelessManager", "This is retry attempt %d", request.retryCount + 1);
            }
            
            initiateHttpRequest(request);
        } else {
            ESP_LOGD("WirelessManager", "Waiting %lu ms before retrying request to %s", 
                     1000 - timeSinceLastAttempt, request.path.c_str());
        }
    }
    
    return true;
}

/**
 * Initiates a new HTTP request using ESP-IDF's HTTP client.
 * This method configures and starts an asynchronous HTTP request.
 * 
 * The request lifecycle:
 * 1. Validates WiFi connection
 * 2. Initializes ESP HTTP client with configuration
 * 3. Sets up method and headers
 * 4. Initiates the asynchronous request
 * 
 * @param request Reference to the HttpRequest to process
 */
void WifiState::initiateHttpRequest(HttpRequest& request) {
    // Construct full URL by combining base URL and path
    String fullUrl;
    // Check if baseUrl already contains http:// or https://
    if (strncmp(baseUrl, "http://", 7) == 0 || strncmp(baseUrl, "https://", 8) == 0) {
        fullUrl = String(baseUrl);
        // Ensure we don't have double slashes between baseUrl and path
        if (baseUrl[strlen(baseUrl)-1] == '/' && request.path[0] == '/') {
            fullUrl += (request.path.c_str() + 1); // Skip the first slash of the path
        } else if (baseUrl[strlen(baseUrl)-1] != '/' && request.path[0] != '/') {
            fullUrl += "/" + request.path;  // Add slash between baseUrl and path
        } else {
            fullUrl += request.path;  // Use as is
        }
    } else {
        // Add http:// prefix if missing
        fullUrl = String("http://") + String(baseUrl);
        // Handle slashes same as above
        if (baseUrl[strlen(baseUrl)-1] == '/' && request.path[0] == '/') {
            fullUrl += (request.path.c_str() + 1);
        } else if (baseUrl[strlen(baseUrl)-1] != '/' && request.path[0] != '/') {
            fullUrl += "/" + request.path;
        } else {
            fullUrl += request.path;
        }
    }
    
    ESP_LOGI("WirelessManager", "Initiating HTTP request: URL: %s, Method: %s", 
             fullUrl.c_str(), request.method.c_str());
    
    // Check WiFi status with detailed logging
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGE("WirelessManager", "Cannot make HTTP request - WiFi not connected (status: %d)", WiFi.status());
        handleRequestError(request, {
            WirelessError::WIFI_NOT_CONNECTED,
            "WiFi connection lost"
        });
        return;
    } else {
        ESP_LOGI("WirelessManager", "WiFi connected to: %s, IP: %s, RSSI: %d dBm", 
                 WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    // Check HTTP client with detailed logging
    if (!httpClient) {
        ESP_LOGW("WirelessManager", "HTTP client not initialized, attempting to initialize");
        if (!initializeHttpClient()) {
            ESP_LOGE("WirelessManager", "Failed to initialize HTTP client");
            handleRequestError(request, {
                WirelessError::CONNECTION_FAILED,
                "Failed to initialize HTTP client"
            });
            return;
        }
        ESP_LOGI("WirelessManager", "HTTP client initialized successfully");
    }

    // Clear any previous response data
    request.responseData = "";
    ESP_LOGI("WirelessManager", "Cleared previous response data");
    
    // Set request state
    request.inProgress = true;
    request.lastAttemptTime = millis();
    currentRequest = &request;  // Set current request for event handler
    ESP_LOGI("WirelessManager", "Request marked as in-progress, attempt time: %lu", request.lastAttemptTime);

    // Configure client for this request with detailed logging
    ESP_LOGI("WirelessManager", "Configuring HTTP client for URL: %s", fullUrl.c_str());
    esp_http_client_set_url(httpClient, fullUrl.c_str());
    
    // Set method with detailed logging
    if (request.method == "POST") {
        ESP_LOGI("WirelessManager", "Setting HTTP method to POST");
        esp_http_client_set_method(httpClient, HTTP_METHOD_POST);
    } else {
        ESP_LOGI("WirelessManager", "Setting HTTP method to GET");
        esp_http_client_set_method(httpClient, HTTP_METHOD_GET);
    }

    // Set headers for POST with detailed logging
    if (request.method == "POST") {
        ESP_LOGI("WirelessManager", "Setting Content-Type header to application/json");
        esp_http_client_set_header(httpClient, "Content-Type", "application/json");
        
        ESP_LOGI("WirelessManager", "Setting POST payload: %s (length: %d)", 
                 request.payload.c_str(), request.payload.length());
        esp_http_client_set_post_field(httpClient, request.payload.c_str(), request.payload.length());
    }

    // Perform request with detailed logging
    ESP_LOGI("WirelessManager", "Performing HTTP request to: %s", fullUrl.c_str());
    esp_err_t err = esp_http_client_perform(httpClient);
    
    if (err != ESP_OK) {
        ESP_LOGE("WirelessManager", "HTTP request failed with error: %s (code: %d)", 
                 esp_err_to_name(err), err);
        handleRequestError(request, {
            WirelessError::CONNECTION_FAILED,
            "Failed to perform HTTP request: " + String(esp_err_to_name(err))
        });
        
        // Reset client on error with detailed logging
        ESP_LOGW("WirelessManager", "Resetting HTTP client due to error");
        cleanupHttpClient();
        ESP_LOGI("WirelessManager", "Reinitializing HTTP client");
        initializeHttpClient();
        currentRequest = nullptr;
        httpQueue->pop();
        ESP_LOGI("WirelessManager", "Request removed from queue, queue size: %d", httpQueue->size());
    } else {
        ESP_LOGI("WirelessManager", "HTTP request initiated successfully");
    }
}

/**
 * Monitors ongoing requests for timeouts and manages retries.
 * This method is called periodically from the WiFi state loop.
 * 
 * Timeout handling:
 * - Checks if request has exceeded timeout period (5000ms)
 * - Increments retry counter if timed out
 * - Cleans up resources if max retries reached
 * - Removes failed requests from queue
 */
void WifiState::checkOngoingRequests() {
    static unsigned long lastDetailedLogTime = 0;
    unsigned long currentTime = millis();
    
    // Provide detailed logging every 5 seconds
    bool detailedLogging = (currentTime - lastDetailedLogTime > 5000);
    if (detailedLogging) {
        lastDetailedLogTime = currentTime;
    }
    
    if (httpQueue->empty()) {
        if (detailedLogging) {
            ESP_LOGI("WirelessManager", "HTTP queue is empty, no requests to check");
        }
        return;
    }

    HttpRequest& request = httpQueue->front();
    
    if (!request.inProgress) {
        if (detailedLogging) {
            ESP_LOGI("WirelessManager", "Front request to %s is not in progress", request.path.c_str());
        }
        return;
    }
    
    // Calculate elapsed time since request started
    unsigned long elapsedTime = currentTime - request.lastAttemptTime;
    
    // Log request status periodically
    if (detailedLogging) {
        ESP_LOGI("WirelessManager", "Ongoing request to %s (method: %s)", 
                 request.path.c_str(), request.method.c_str());
        ESP_LOGI("WirelessManager", "Request elapsed time: %lu ms, retry count: %d", 
                 elapsedTime, request.retryCount);
    }
    
    // Check for timeout (10 seconds)
    if (elapsedTime > 10000) {
        ESP_LOGE("WirelessManager", "Request to %s timed out after %lu ms", 
                 request.path.c_str(), elapsedTime);
        
        // Increment retry counter
        request.retryCount++;
        ESP_LOGI("WirelessManager", "Incremented retry count to %d", request.retryCount);
        
        // Reset request state
        request.inProgress = false;
        currentRequest = nullptr;
        
        // Check if max retries reached
        if (request.retryCount >= 3) {
            ESP_LOGE("WirelessManager", "Max retries reached for path: %s", request.path.c_str());
            
            // Call error callback if available
            if (request.hasErrorCallback) {
                ESP_LOGI("WirelessManager", "Calling error callback for timed out request");
                request.onError({
                    WirelessError::TIMEOUT,
                    "Request timed out after 3 retries"
                });
            }
            
            // Remove from queue
            httpQueue->pop();
            ESP_LOGI("WirelessManager", "Failed request removed from queue, queue size: %d", 
                     httpQueue->size());
        } else {
            ESP_LOGI("WirelessManager", "Will retry request (attempt %d of 3)", 
                     request.retryCount + 1);
        }
        
        // Reset HTTP client after timeout
        ESP_LOGW("WirelessManager", "Resetting HTTP client due to timeout");
        cleanupHttpClient();
        ESP_LOGI("WirelessManager", "Reinitializing HTTP client");
        initializeHttpClient();
    } else if (detailedLogging) {
        ESP_LOGI("WirelessManager", "Request to %s is still in progress (%lu ms elapsed)", 
                 request.path.c_str(), elapsedTime);
    }
}

/**
 * Handles request errors by invoking error callback or logging.
 * 
 * @param request The request that encountered an error
 * @param error Information about the error that occurred
 */
void WifiState::handleRequestError(HttpRequest& request, WirelessErrorInfo error) {
    ESP_LOGE("WirelessManager", "Request error: %s, Path: %s, Method: %s", 
             error.message.c_str(), request.path.c_str(), request.method.c_str());
    if (request.hasErrorCallback) {
        request.onError(error);
    }
    // Implement retry logic with exponential backoff
    if (error.code == WirelessError::CONNECTION_FAILED && request.retryCount < MAX_RETRIES) {
        unsigned long backoffTime = pow(2, request.retryCount) * 1000; // Exponential backoff
        request.lastAttemptTime = millis() + backoffTime;
        ESP_LOGI("WirelessManager", "Retrying request in %lu ms", backoffTime);
    } else {
        ESP_LOGW("WirelessManager", "Max retries reached or non-recoverable error");
        httpQueue->pop();
    }
}

// WirelessManager Implementation
WirelessManager::WirelessManager(Device* device, const char* wifiSsid, const char* wifiPassword, const char* baseUrl)
    : StateMachine(device)
    , wifiSsid(wifiSsid)
    , wifiPassword(wifiPassword)
    , baseUrl(baseUrl)
    , pendingEspNowSwitch(false)
    , pendingWifiSwitch(false)
    , pendingPowerOff(false) {
    ESP_LOGI("WirelessManager", "WirelessManager created with SSID: %s, base URL: %s", wifiSsid, baseUrl);
}

void WirelessManager::populateStateMap() {
    ESP_LOGI("WirelessManager", "Populating state map with SSID: %s, base URL: %s", wifiSsid, baseUrl);
    
    // Create states
    PowerOffState* powerOffState = new PowerOffState();
    EspNowState* espNowState = new EspNowState();
    WifiState* wifiState = new WifiState(wifiSsid, wifiPassword, baseUrl, &httpQueue);
    
    // Add states to the map
    stateMap.push_back(powerOffState);
    ESP_LOGI("WirelessManager", "Added PowerOffState at index 0");
    
    stateMap.push_back(espNowState);
    ESP_LOGI("WirelessManager", "Added EspNowState at index 1");
    
    stateMap.push_back(wifiState);
    ESP_LOGI("WirelessManager", "Added WifiState at index 2");
    
    // Add transitions between states based on pending flags
    
    // Transitions from PowerOffState
    powerOffState->addTransition(new StateTransition(
        [this]() { return pendingWifiSwitch; },
        wifiState
    ));
    ESP_LOGI("WirelessManager", "Added transition from PowerOffState to WifiState");
    
    powerOffState->addTransition(new StateTransition(
        [this]() { return pendingEspNowSwitch; },
        espNowState
    ));
    ESP_LOGI("WirelessManager", "Added transition from PowerOffState to EspNowState");
    
    // Transitions from EspNowState
    espNowState->addTransition(new StateTransition(
        [this]() { return pendingWifiSwitch; },
        wifiState
    ));
    ESP_LOGI("WirelessManager", "Added transition from EspNowState to WifiState");
    
    espNowState->addTransition(new StateTransition(
        [this]() { return pendingPowerOff; },
        powerOffState
    ));
    ESP_LOGI("WirelessManager", "Added transition from EspNowState to PowerOffState");
    
    // Transitions from WifiState
    wifiState->addTransition(new StateTransition(
        [this]() { return pendingEspNowSwitch; },
        espNowState
    ));
    ESP_LOGI("WirelessManager", "Added transition from WifiState to EspNowState");
    
    wifiState->addTransition(new StateTransition(
        [this]() { return pendingPowerOff; },
        powerOffState
    ));
    ESP_LOGI("WirelessManager", "Added transition from WifiState to PowerOffState");
}

bool WirelessManager::makeHttpRequest(
    const String& path,
    std::function<void(const String&)> onSuccess,
    std::function<void(const WirelessErrorInfo&)> onError,
    const String& method,
    const String& payload
) {
    ESP_LOGI("WirelessManager", "Queueing HTTP request to path: %s (method: %s)", path.c_str(), method.c_str());
    
    // Create and queue the request
    httpQueue.emplace(path, onSuccess, onError, method, payload);
    
    // Log queue size after adding the request
    ESP_LOGI("WirelessManager", "HTTP queue size after adding request: %d", httpQueue.size());
    
    // Only switch to WiFi if we're not already in that state
    if (getCurrentState()->getStateId() != WirelessStateId::WIFI) {
        ESP_LOGI("WirelessManager", "Not in WiFi state, switching to WiFi state");
        switchToWifi();
    } else {
        ESP_LOGI("WirelessManager", "Already in WiFi state, request will be processed in the next loop");
    }
    
    return true;
}

bool WirelessManager::makeHttpRequest(
    const String& path,
    std::function<void(const String&)> onSuccess,
    const String& method,
    const String& payload
) {
    ESP_LOGI("WirelessManager", "Queueing HTTP request to path: %s (method: %s, no error callback)", 
             path.c_str(), method.c_str());
    
    // Create and queue the request
    httpQueue.emplace(path, onSuccess, method, payload);
    
    // Log queue size after adding the request
    ESP_LOGI("WirelessManager", "HTTP queue size after adding request: %d", httpQueue.size());
    
    // Only switch to WiFi if we're not already in that state
    if (getCurrentState()->getStateId() != WirelessStateId::WIFI) {
        ESP_LOGI("WirelessManager", "Not in WiFi state, switching to WiFi state");
        switchToWifi();
    } else {
        ESP_LOGI("WirelessManager", "Already in WiFi state, request will be processed in the next loop");
    }
    
    return true;
}

bool WirelessManager::switchToEspNow() {
    pendingEspNowSwitch = true;
    pendingWifiSwitch = false;
    pendingPowerOff = false;
    return true;
}

bool WirelessManager::switchToWifi() {
    pendingWifiSwitch = true;
    pendingEspNowSwitch = false;
    pendingPowerOff = false;
    return true;
}

bool WirelessManager::powerOff() {
    pendingPowerOff = true;
    pendingEspNowSwitch = false;
    pendingWifiSwitch = false;
    return true;
}

void WirelessManager::initialize() {
    ESP_LOGI("WirelessManager", "Initializing WirelessManager");
    
    // Call base class initialize which will populate state map and set initial state
    StateMachine::initialize();
    
    ESP_LOGI("WirelessManager", "WirelessManager initialized with initial state: %d", 
             getCurrentState()->getStateId());
    
    // Immediately switch to WiFi state if we have HTTP requests
    if (!httpQueue.empty()) {
        ESP_LOGI("WirelessManager", "HTTP queue not empty, switching to WiFi state");
        switchToWifi();
    }
}

void WirelessManager::loop() {
    static unsigned long lastStatusLog = 0;
    static int lastStateId = -1;
    
    // Get current state ID
    int currentStateId = getCurrentState()->getStateId();
    
    // Check if state has changed
    if (currentStateId != lastStateId) {
        ESP_LOGI("WirelessManager", "State changed from %d to %d", lastStateId, currentStateId);
        
        // Reset pending flags based on the new state
        if (currentStateId == WirelessStateId::WIFI && pendingWifiSwitch) {
            ESP_LOGI("WirelessManager", "Successfully switched to WiFi state");
            pendingWifiSwitch = false;
        } else if (currentStateId == WirelessStateId::ESP_NOW && pendingEspNowSwitch) {
            ESP_LOGI("WirelessManager", "Successfully switched to ESP-NOW state");
            pendingEspNowSwitch = false;
        } else if (currentStateId == WirelessStateId::POWER_OFF && pendingPowerOff) {
            ESP_LOGI("WirelessManager", "Successfully switched to Power Off state");
            pendingPowerOff = false;
        }
        
        lastStateId = currentStateId;
    }
    
    // Log status every 5 seconds instead of every second
    if (millis() - lastStatusLog > 5000) {
        
        // If in WiFi state, log WiFi status
        if (currentStateId == WirelessStateId::WIFI) {
            // Only log connection status if it's connected or has changed
            static wl_status_t lastWiFiStatus = WL_IDLE_STATUS;
            wl_status_t currentWiFiStatus = WiFi.status();
            
            if (currentWiFiStatus != lastWiFiStatus || currentWiFiStatus == WL_CONNECTED) {
                ESP_LOGI("WirelessManager", "WiFi status: %d", currentWiFiStatus);
                
                if (currentWiFiStatus == WL_CONNECTED) {
                    ESP_LOGI("WirelessManager", "WiFi connected to: %s, IP: %s, RSSI: %d dBm", 
                             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
                } else if (currentWiFiStatus != lastWiFiStatus) {
                    ESP_LOGW("WirelessManager", "WiFi not connected, status: %d", currentWiFiStatus);
                }
                
                lastWiFiStatus = currentWiFiStatus;
            }
            
            // Only log queue size if it's not empty
            if (!httpQueue.empty()) {
                ESP_LOGI("WirelessManager", "HTTP queue size: %d", httpQueue.size());
            }
        }
        
        lastStatusLog = millis();
    }
    
    // Call base class loop which will handle state transitions
    StateMachine::loop();
} 