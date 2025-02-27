#include "wireless/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "device.hpp"
#include <esp_log.h>
#include <memory>
#include <utility>
#include <string>

PowerOffState::PowerOffState() : WirelessState(WirelessStateId::POWER_OFF) {}

EspNowState::EspNowState() : WirelessState(WirelessStateId::ESP_NOW) {}

WifiState::WifiState(const char* wifiSsid, const char* wifiPassword, std::queue<HttpRequest>* requestQueue) 
    : WirelessState(WirelessStateId::WIFI)
    , ssid(wifiSsid)
    , password(wifiPassword)
    , httpQueue(requestQueue)
    , currentRequest(nullptr) {}

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
    if (initializeWiFi()) {
        ESP_LOGI("WirelessManager", "WiFi initialized successfully");
        
        // Configure WiFiClientSecure to skip certificate verification
        wifiClientSecure.setInsecure();
        
        // Set additional WiFiClientSecure options for better reliability
        wifiClientSecure.setTimeout(30); // 30 seconds timeout
        wifiClientSecure.setHandshakeTimeout(30000); // 30 seconds for handshake
        
        ESP_LOGI("WirelessManager", "WiFiClientSecure configured with extended timeouts");
        ESP_LOGI("WirelessManager", "WiFiClientSecure configured to skip certificate verification");
    } else {
        ESP_LOGE("WirelessManager", "Failed to initialize WiFi");
    }
}

void WifiState::onStateLoop(Device* device) {
    idleTimer.updateTime();
    processQueuedRequests();
    checkOngoingRequests();
}

void WifiState::onStateDismounted(Device* device) {
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
    
    // Log request status
    if (request.inProgress) {
        ESP_LOGD("WirelessManager", "Front request to %s is already in progress", 
                 request.url.c_str());
    } else {
        // Check if we should initiate the request now
        unsigned long timeSinceLastAttempt = 
            request.lastAttemptTime > 0 ? currentTime - request.lastAttemptTime : ULONG_MAX;
            
        if (timeSinceLastAttempt > 1000 || request.lastAttemptTime == 0) {
            ESP_LOGI("WirelessManager", "Initiating request to %s (method: %s)", 
                     request.url.c_str(), request.method.c_str());
            
            // If this is a retry, log retry count
            if (request.retryCount > 0) {
                ESP_LOGI("WirelessManager", "This is retry attempt %d", request.retryCount + 1);
            }
            
            initiateHttpRequest(request);
        } else {
            ESP_LOGD("WirelessManager", "Waiting %lu ms before retrying request to %s", 
                     1000 - timeSinceLastAttempt, request.url.c_str());
        }
    }
    
    return true;
}

/**
 * Initiates a new HTTP request using WiFiClientSecure and HTTPClient.
 * This method configures and performs an HTTP request.
 * 
 * The request lifecycle:
 * 1. Validates WiFi connection
 * 2. Creates an HTTPClient instance
 * 3. Sets up method and headers
 * 4. Performs the request and processes the response
 * 
 * @param request Reference to the HttpRequest to process
 */
void WifiState::initiateHttpRequest(HttpRequest& request) {
    ESP_LOGI("WirelessManager", "Initiating HTTP request: URL: %s, Method: %s", 
             request.url.c_str(), request.method.c_str());
    
    // Check WiFi status with detailed logging
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGE("WirelessManager", "Cannot make HTTP request - WiFi not connected (status: %d)", WiFi.status());
        handleRequestError(request, {
            WirelessError::WIFI_NOT_CONNECTED,
            "WiFi connection lost"
        });
        return;
    } else {
        ESP_LOGI("WirelessManager", "WiFi connected to %s, IP: %s, RSSI: %d dBm",
                 WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    // Clear any previous response data
    request.responseData = "";
    ESP_LOGI("WirelessManager", "Cleared previous response data");
    
    // Set request state
    request.inProgress = true;
    request.lastAttemptTime = millis();
    currentRequest = &request;
    ESP_LOGI("WirelessManager", "Request marked as in-progress, attempt time: %lu", request.lastAttemptTime);

    // Log memory usage before request
    ESP_LOGI("WirelessManager", "Free heap before request: %d bytes", ESP.getFreeHeap());

    // Create HTTP client
    HTTPClient http;
    
    // Set a longer timeout (30 seconds instead of the default 5)
    http.setTimeout(30000);
    ESP_LOGI("WirelessManager", "Set HTTP client timeout to 30 seconds");
    
    // Check if URL starts with https
    bool isHttps = request.url.substr(0, 5) == "https";
    
    // Begin the request with the appropriate client
    if (isHttps) {
        ESP_LOGI("WirelessManager", "Using secure client for HTTPS request");
        http.begin(wifiClientSecure, request.url.c_str());
    } else {
        ESP_LOGI("WirelessManager", "Using regular client for HTTP request");
        http.begin(request.url.c_str());
    }
    
    // Add a small delay to ensure connection stability
    delay(100);
    
    // Set content type for requests with payload
    if (!request.payload.empty()) {
        http.addHeader("Content-Type", "application/json");
        ESP_LOGI("WirelessManager", "Added Content-Type: application/json header");
    }
    
    // Perform the request based on the method
    int httpCode = -1;
    
    if (request.method == "GET") {
        ESP_LOGI("WirelessManager", "Performing GET request");
        httpCode = http.GET();
    } else if (request.method == "POST") {
        ESP_LOGI("WirelessManager", "Performing POST request with payload: %s", request.payload.c_str());
        ESP_LOGI("WirelessManager", "Payload size: %d bytes", request.payload.length());
        httpCode = http.POST(request.payload.c_str());
    } else if (request.method == "PUT") {
        ESP_LOGI("WirelessManager", "Performing PUT request with payload: %s", request.payload.c_str());
        ESP_LOGI("WirelessManager", "Payload size: %d bytes", request.payload.length());
        httpCode = http.PUT(request.payload.c_str());
    } else {
        ESP_LOGE("WirelessManager", "Unsupported HTTP method: %s", request.method.c_str());
        handleRequestError(request, {
            WirelessError::INVALID_STATE,
            "Unsupported HTTP method: " + request.method
        });
        http.end();
        return;
    }
    
    // Check HTTP response code
    if (httpCode > 0) {
        ESP_LOGI("WirelessManager", "HTTP response code: %d", httpCode);
        
        // HTTP request was successful
        if (httpCode >= 200 && httpCode < 300) {
            // Get the response payload
            std::string payload = http.getString().c_str();
            request.responseData = payload;
            
            ESP_LOGI("WirelessManager", "Request successful, response length: %d", payload.length());
            
            // Call success callback
            if (request.onSuccess) {
                ESP_LOGI("WirelessManager", "Calling success callback");
                request.onSuccess(payload);
            }
            
            // Request completed successfully, remove from queue
            httpQueue->pop();
            currentRequest = nullptr;
        } else {
            // HTTP error
            ESP_LOGE("WirelessManager", "HTTP error: %d", httpCode);
            
            // Handle error based on HTTP code
            WirelessError errorCode = httpCode >= 500 ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE;
            std::string errorMessage = "HTTP Error: " + std::to_string(httpCode);
            
            handleRequestError(request, {
                errorCode,
                errorMessage
            });
        }
    } else {
        // HTTP request failed
        ESP_LOGE("WirelessManager", "HTTP request failed: %s", http.errorToString(httpCode).c_str());
        
        // Check if it's a timeout error
        if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
            ESP_LOGW("WirelessManager", "Read timeout detected. Server might be slow to respond.");
            ESP_LOGI("WirelessManager", "This is common with free-tier services like Render that have cold starts.");
            
            // For timeout errors, we'll use a more aggressive retry strategy
            if (request.retryCount < MAX_RETRIES) {
                request.retryCount++;
                request.inProgress = false;
                
                // Use a shorter backoff for timeouts since the server might just be slow
                unsigned long backoffTime = 2000; // 2 seconds fixed backoff for timeouts
                
                ESP_LOGI("WirelessManager", "Will retry after timeout in %lu ms (attempt %d of %d)", 
                         backoffTime, request.retryCount + 1, MAX_RETRIES + 1);
                
                // End the HTTP client before returning
                http.end();
                
                // Log memory usage after request
                ESP_LOGI("WirelessManager", "Free heap after timeout retry decision: %d bytes", ESP.getFreeHeap());
                
                return; // Keep the request in the queue for retry
            }
        }
        
        handleRequestError(request, {
            WirelessError::CONNECTION_FAILED,
            "HTTP request failed: " + std::string(http.errorToString(httpCode).c_str())
        });
    }
    
    // End the HTTP client
    http.end();
    
    // Log memory usage after request
    ESP_LOGI("WirelessManager", "Free heap after request: %d bytes", ESP.getFreeHeap());
}

/**
 * Monitors ongoing requests for timeouts and manages retries.
 * This method is called periodically from the WiFi state loop.
 * 
 * Timeout handling:
 * - Checks if request has exceeded timeout period (10000ms)
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
            ESP_LOGI("WirelessManager", "Front request to %s is not in progress", request.url.c_str());
        }
        return;
    }
    
    // Calculate elapsed time since request started
    unsigned long elapsedTime = currentTime - request.lastAttemptTime;
    
    // Log request status periodically
    if (detailedLogging) {
        ESP_LOGI("WirelessManager", "Ongoing request to %s (method: %s)", 
                 request.url.c_str(), request.method.c_str());
        ESP_LOGI("WirelessManager", "Request elapsed time: %lu ms, retry count: %d", 
                 elapsedTime, request.retryCount);
    }
    
    // Check for timeout (10 seconds)
    if (elapsedTime > 10000) {
        ESP_LOGE("WirelessManager", "Request to %s timed out after %lu ms", 
                 request.url.c_str(), elapsedTime);
        
        // Increment retry counter
        request.retryCount++;
        ESP_LOGI("WirelessManager", "Incremented retry count to %d", request.retryCount);
        
        // Reset request state
        request.inProgress = false;
        currentRequest = nullptr;
        
        // Check if max retries reached
        if (request.retryCount >= 3) {
            ESP_LOGE("WirelessManager", "Max retries reached for URL: %s", request.url.c_str());
            
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
    } else if (detailedLogging) {
        ESP_LOGI("WirelessManager", "Request to %s is still in progress (%lu ms elapsed)", 
                 request.url.c_str(), elapsedTime);
    }
}

/**
 * Handles request errors by invoking error callback or logging.
 * 
 * @param request The request that encountered an error
 * @param error Information about the error that occurred
 */
void WifiState::handleRequestError(HttpRequest& request, WirelessErrorInfo error) {
    ESP_LOGE("WirelessManager", "Request error: %s, URL: %s, Method: %s", 
             error.message.c_str(), request.url.c_str(), request.method.c_str());
    
    // Call error callback if available
    if (request.hasErrorCallback) {
        request.onError(error);
    }
    
    // Implement retry logic with exponential backoff
    if (error.code == WirelessError::CONNECTION_FAILED && request.retryCount < MAX_RETRIES) {
        request.retryCount++;
        unsigned long backoffTime = pow(2, request.retryCount) * 1000; // Exponential backoff
        request.lastAttemptTime = millis();
        request.inProgress = false;
        ESP_LOGI("WirelessManager", "Retrying request in %lu ms (attempt %d of %d)", 
                 backoffTime, request.retryCount + 1, MAX_RETRIES + 1);
    } else {
        ESP_LOGW("WirelessManager", "Max retries reached or non-recoverable error");
        httpQueue->pop();
        currentRequest = nullptr;
    }
}

// WirelessManager Implementation
WirelessManager::WirelessManager(Device* device, const char* wifiSsid, const char* wifiPassword)
    : StateMachine(device)
    , wifiSsid(wifiSsid)
    , wifiPassword(wifiPassword)
    , pendingEspNowSwitch(false)
    , pendingWifiSwitch(false)
    , pendingPowerOff(false) {
    ESP_LOGI("WirelessManager", "WirelessManager created with SSID: %s", wifiSsid);
}

void WirelessManager::populateStateMap() {
    ESP_LOGI("WirelessManager", "Populating state map with SSID: %s", wifiSsid);
    
    // Create states
    PowerOffState* powerOffState = new PowerOffState();
    EspNowState* espNowState = new EspNowState();
    WifiState* wifiState = new WifiState(wifiSsid, wifiPassword, &httpQueue);
    
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
    const std::string& url,
    std::function<void(const std::string&)> onSuccess,
    std::function<void(const WirelessErrorInfo&)> onError,
    const std::string& method,
    const std::string& payload
) {
    ESP_LOGI("WirelessManager", "Queueing HTTP request to: %s (method: %s)", url.c_str(), method.c_str());
    
    // Create and queue the request
    httpQueue.emplace(url, onSuccess, onError, method, payload);
    
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
    const std::string& url,
    std::function<void(const std::string&)> onSuccess,
    const std::string& method,
    const std::string& payload
) {
    ESP_LOGI("WirelessManager", "Queueing HTTP request to: %s (method: %s, no error callback)", 
             url.c_str(), method.c_str());
    
    // Create and queue the request
    httpQueue.emplace(url, onSuccess, method, payload);
    
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
    
    // Log status every 5 seconds
    if (millis() - lastStatusLog > 5000) {
        ESP_LOGI("WirelessManager", "Current state: %d", currentStateId);
        
        // If in WiFi state, log WiFi status
        if (currentStateId == WirelessStateId::WIFI) {
            ESP_LOGI("WirelessManager", "WiFi status: %d", WiFi.status());
            if (WiFi.status() == WL_CONNECTED) {
                ESP_LOGI("WirelessManager", "WiFi connected to %s, IP: %s, RSSI: %d dBm", 
                         WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
            } else {
                ESP_LOGW("WirelessManager", "WiFi not connected, status: %d", WiFi.status());
            }
            
            // Log HTTP queue size
            ESP_LOGI("WirelessManager", "HTTP queue size: %d", httpQueue.size());
        }
        
        lastStatusLog = millis();
    }
    
    // Call base class loop which will handle state transitions
    StateMachine::loop();
} 