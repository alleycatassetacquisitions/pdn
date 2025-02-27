#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <queue>
#include <memory>
#include <functional>
#include <string>

#include "state-machine.hpp"
#include "wireless/wireless-types.hpp"
#include "simple-timer.hpp"

// Forward declarations
class Device;
class State;
class StateMachine;

enum WirelessStateId {
    POWER_OFF = 0,
    ESP_NOW = 1,
    WIFI = 2
};

/**
 * Represents an HTTP request with associated callbacks and state.
 * This struct manages both the request configuration and its runtime state.
 */
struct HttpRequest {
    std::string url;                                  ///< URL for the request
    std::function<void(const std::string&)> onSuccess; ///< Callback for successful requests
    std::function<void(const WirelessErrorInfo&)> onError; ///< Callback for failed requests
    std::string method = "GET";                       ///< HTTP method (GET, POST, etc.)
    std::string payload = "";                         ///< Request payload (for POST/PUT requests)
    bool hasErrorCallback = false;                    ///< Whether an error callback was provided
    bool inProgress = false;                          ///< Whether the request is currently in progress
    unsigned long lastAttemptTime = 0;                ///< Time of the last attempt
    int retryCount = 0;                               ///< Number of retry attempts
    std::string responseData = "";                    ///< Accumulated response data

    /**
     * Creates an HTTP request with both success and error callbacks.
     * 
     * @param requestUrl The target URL for the request
     * @param successCallback Function to call on successful response
     * @param errorCallback Function to call on error
     * @param requestMethod HTTP method to use (defaults to "GET")
     * @param requestPayload Request body for POST/PUT requests
     */
    HttpRequest(const std::string& requestUrl, 
                std::function<void(const std::string&)> successCallback,
                std::function<void(const WirelessErrorInfo&)> errorCallback,
                const std::string& requestMethod = "GET", 
                const std::string& requestPayload = "")
        : url(requestUrl), 
          method(requestMethod), 
          payload(requestPayload), 
          onSuccess(successCallback), 
          onError(errorCallback),
          hasErrorCallback(true),
          retryCount(0), 
          lastAttemptTime(0), 
          inProgress(false) {}
    
    /**
     * Creates an HTTP request with only a success callback.
     * Errors will be logged but not handled by user code.
     * 
     * @param requestUrl The target URL for the request
     * @param successCallback Function to call on successful response
     * @param requestMethod HTTP method to use (defaults to "GET")
     * @param requestPayload Request body for POST/PUT requests
     */
    HttpRequest(const std::string& requestUrl, 
                std::function<void(const std::string&)> successCallback,
                const std::string& requestMethod = "GET", 
                const std::string& requestPayload = "")
        : url(requestUrl), 
          method(requestMethod), 
          payload(requestPayload), 
          onSuccess(successCallback),
          hasErrorCallback(false),
          retryCount(0), 
          lastAttemptTime(0), 
          inProgress(false) {}
};

class WirelessState : public State {
protected:
    WirelessState(int stateId) : State(stateId) {}
    static const unsigned long IDLE_TIMEOUT = 300000; // 5 minutes
    static const uint8_t MAX_RETRIES = 3;
    SimpleTimer idleTimer;
};

class PowerOffState : public WirelessState {
public:
    PowerOffState();
    void onStateMounted(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
private:
    void powerDown();
};

class EspNowState : public WirelessState {
public:
    EspNowState();
    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
};

class WifiState : public WirelessState {
public:
    WifiState(const char* ssid, const char* password, std::queue<HttpRequest>* requestQueue);
    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    
private:
    /**
     * Initializes WiFi connection.
     * @return true if WiFi connection initialization successful
     */
    bool initializeWiFi();

    bool processQueuedRequests();
    void initiateHttpRequest(HttpRequest& request);
    void checkOngoingRequests();
    void handleRequestError(HttpRequest& request, WirelessErrorInfo error);
    
    const char* ssid;
    const char* password;
    uint8_t channel;
    std::queue<HttpRequest>* httpQueue;
    HttpRequest* currentRequest;          ///< Pointer to currently processing request
    WiFiClientSecure wifiClientSecure;    ///< Secure client for HTTPS requests
};

class WirelessManager : public StateMachine {
public:
    WirelessManager(Device* device, const char* wifiSsid, const char* wifiPassword);
    
    /**
     * Initialize the wireless manager
     * Sets up states and transitions
     */
    void initialize();
    
    /**
     * Process the state machine and handle wireless operations
     * Should be called in the main loop
     */
    void loop();
    
    void populateStateMap() override;

    /**
     * Queues an HTTP request with full error handling support.
     * This version includes both success and error callbacks for comprehensive
     * request lifecycle management.
     * 
     * The request is queued and will be processed when the WiFi state is active.
     * If not in WiFi state, this will trigger a state transition to WiFi mode.
     * 
     * @param url Target URL for the HTTP request
     * @param onSuccess Callback function for successful response
     * @param onError Callback function for error handling
     * @param method HTTP method to use ("GET", "POST", or "PUT")
     * @param payload Request body for POST/PUT requests
     * @return true if request was successfully queued
     */
    bool makeHttpRequest(
        const std::string& url,
        std::function<void(const std::string&)> onSuccess,
        std::function<void(const WirelessErrorInfo&)> onError,
        const std::string& method = "GET",
        const std::string& payload = ""
    );

    /**
     * Queues a simple HTTP request with basic error handling.
     * This version only includes a success callback. Errors will be logged
     * but not handled by user code.
     * 
     * The request is queued and will be processed when the WiFi state is active.
     * If not in WiFi state, this will trigger a state transition to WiFi mode.
     * 
     * @param url Target URL for the HTTP request
     * @param onSuccess Callback function for successful response
     * @param method HTTP method to use ("GET", "POST", or "PUT")
     * @param payload Request body for POST/PUT requests
     * @return true if request was successfully queued
     */
    bool makeHttpRequest(
        const std::string& url,
        std::function<void(const std::string&)> onSuccess,
        const std::string& method = "GET",
        const std::string& payload = ""
    );

    // Force state changes
    bool switchToEspNow();
    bool switchToWifi();
    bool powerOff();

    friend class WifiState;

private:
    const char* wifiSsid;
    const char* wifiPassword;
    bool pendingEspNowSwitch;
    bool pendingWifiSwitch;
    bool pendingPowerOff;
    std::queue<HttpRequest> httpQueue;
}; 