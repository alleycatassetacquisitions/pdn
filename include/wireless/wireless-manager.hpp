#pragma once

#include <WiFi.h>
#include <esp_http_client.h>
#include <queue>
#include <memory>
#include <functional>

#include "state-machine.hpp"
#include "wireless/wireless-types.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "utils/simple-timer.hpp"
#include "esp-now-comms.hpp"

// Forward declarations
class Device;
class State;
class StateMachine;

// Forward declare the event handler
esp_err_t http_event_handler(esp_http_client_event_t *evt);


enum WirelessStateId {
    POWER_OFF = 0,
    ESP_NOW = 1,
    WIFI = 2
};

/**
 * Represents an HTTP request with associated callbacks and state.
 * This struct manages both the request configuration and its runtime state,
 * integrating with ESP-IDF's esp_http_client for asynchronous operation.
 */
struct HttpRequest {
    std::string path;                                  ///< Path to append to base URL
    std::function<void(const std::string&)> onSuccess; ///< Callback for successful requests
    std::function<void(const WirelessErrorInfo&)> onError; ///< Callback for failed requests
    std::string method = "GET";                       ///< HTTP method (GET, POST, etc.)
    std::string payload = "";                         ///< Request payload (for POST requests)
    bool hasErrorCallback = false;               ///< Whether an error callback was provided
    bool inProgress = false;                     ///< Whether the request is currently in progress
    unsigned long lastAttemptTime = 0;           ///< Time of the last attempt
    int retryCount = 0;                          ///< Number of retry attempts
    std::string responseData = "";                    ///< Accumulated response data
    esp_http_client_handle_t client;             ///< ESP-IDF HTTP client handle

    /**
     * Creates an HTTP request with both success and error callbacks.
     * 
     * @param requestPath The path to append to the base URL
     * @param successCallback Function to call on successful response
     * @param errorCallback Function to call on error
     * @param requestMethod HTTP method to use (defaults to "GET")
     * @param requestPayload Request body for POST requests
     */
    HttpRequest(const std::string& requestPath, 
                std::function<void(const std::string&)> successCallback,
                std::function<void(const WirelessErrorInfo&)> errorCallback,
                const std::string& requestMethod = "GET", 
                const std::string& requestPayload = "")
        : path(requestPath), 
          method(requestMethod), 
          payload(requestPayload), 
          onSuccess(successCallback), 
          onError(errorCallback),
          hasErrorCallback(true),
          retryCount(0), 
          lastAttemptTime(0), 
          inProgress(false),
          client(nullptr) {}
    
    /**
     * Creates an HTTP request with only a success callback.
     * Errors will be logged but not handled by user code.
     * 
     * @param requestPath The path to append to the base URL
     * @param successCallback Function to call on successful response
     * @param requestMethod HTTP method to use (defaults to "GET")
     * @param requestPayload Request body for POST requests
     */
    HttpRequest(const std::string& requestPath, 
                std::function<void(const std::string&)> successCallback,
                const std::string& requestMethod = "GET", 
                const std::string& requestPayload = "")
        : path(requestPath), 
          method(requestMethod), 
          payload(requestPayload), 
          onSuccess(successCallback),
          hasErrorCallback(false),
          retryCount(0), 
          lastAttemptTime(0), 
          inProgress(false),
          client(nullptr) {}
};

class WirelessState : public State {
protected:
    WirelessState(int stateId) : State(stateId) {}
    static const unsigned long IDLE_TIMEOUT = 300000; // 5 minutes
    static const uint8_t MAX_RETRIES = 1;
    SimpleTimer idleTimer;
};

class PowerOffState : public WirelessState {
public:
    PowerOffState();
    void onStateMounted(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
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
    WifiState(WifiConfig* config, std::queue<HttpRequest>* requestQueue);
    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    
private:
    /**
     * Checks if WiFi is connected (non-blocking).
     * @return true if WiFi is connected
     */
    bool initializeWiFi();

    /**
     * Initializes the persistent HTTP client with default configuration.
     * @return true if client initialization successful
     */
    bool initializeHttpClient();

    /**
     * Cleans up HTTP client resources.
     */
    void cleanupHttpClient();

    /**
     * Starts the WiFi connection process.
     */
    void startWifiConnectionProcess();

    /**
     * Logs detailed WiFi status errors.
     */
    void logWifiStatusError();

    bool processQueuedRequests();
    void initiateHttpRequest(HttpRequest& request);
    void checkOngoingRequests();
    void handleRequestError(HttpRequest& request, WirelessErrorInfo error);
    
    // WiFi connection state
    bool wifiConnected;
    bool httpClientInitialized;
    int wifiConnectionAttempts;
    SimpleTimer connectionAttemptTimer;
    static const int MAX_WIFI_CONN_ATTEMPTS = 20;
    
    WifiConfig* wifiConfig;
    uint8_t channel;
    std::queue<HttpRequest>* httpQueue;
    esp_http_client_handle_t httpClient;  ///< Persistent HTTP client handle
    HttpRequest* currentRequest;          ///< Pointer to currently processing request
    
    friend esp_err_t http_event_handler(esp_http_client_event_t *evt);
};

class WirelessManager : public StateMachine {
public:
    WirelessManager(Device* device, const std::string& wifiSsid, const std::string& wifiPassword, const std::string& baseUrl);
    
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
     * The path will be appended to the base URL.
     * 
     * @param path Path to append to base URL
     * @param onSuccess Callback function for successful response
     * @param onError Callback function for error handling
     * @param method HTTP method to use ("GET" or "POST")
     * @param payload Request body for POST requests
     * @return true if request was successfully queued
     */
    bool makeHttpRequest(
        const std::string& path,
        std::function<void(const std::string&)> onSuccess,
        std::function<void(const WirelessErrorInfo&)> onError,
        const std::string& method = "GET",
        const std::string& payload = ""
    );

    /**
     * Queues a simple HTTP request with basic error handling.
     * The path will be appended to the base URL.
     * 
     * @param path Path to append to base URL
     * @param onSuccess Callback function for successful response
     * @param method HTTP method to use ("GET" or "POST")
     * @param payload Request body for POST requests
     * @return true if request was successfully queued
     */
    bool makeHttpRequest(
        const std::string& path,
        std::function<void(const std::string&)> onSuccess,
        const std::string& method = "GET",
        const std::string& payload = ""
    );

    // Force state changes
    bool switchToEspNow();
    bool switchToWifi();
    bool powerOff();
    
    /**
     * Updates WiFi credentials and base URL.
     * If currently connected to WiFi, this will trigger a reconnect.
     * 
     * @param debugPacket Debug packet containing new credentials
     */
    void updateWifiCredentials(DebugPacket debugPacket);

    friend class WifiState;

private:
    WifiConfig wifiConfig;
    bool pendingEspNowSwitch;
    bool pendingWifiSwitch;
    bool pendingPowerOff;
    std::queue<HttpRequest> httpQueue;
}; 