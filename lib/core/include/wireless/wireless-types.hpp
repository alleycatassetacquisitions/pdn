#pragma once

#include <functional>
#include <string>
#include <utility>

enum class WirelessError {
    TIMEOUT,
    CONNECTION_FAILED,
    INVALID_RESPONSE,
    SERVER_ERROR,
    WIFI_NOT_CONNECTED,
    INVALID_STATE
};

struct WirelessErrorInfo {
    WirelessError code;
    std::string message;
};

/**
 * Configuration struct for WiFi credentials and settings
 */
struct WifiConfig {
    std::string ssid;
    std::string password;
    std::string baseUrl;
    
    WifiConfig() : ssid(""), password(""), baseUrl("") {}
    
    WifiConfig(const std::string& ssid, const std::string& password, const std::string& baseUrl)
        : ssid(ssid), password(password), baseUrl(baseUrl) {}
};

// Callback definitions
using HttpSuccessCallback = std::function<void(const std::string& jsonResponse)>;
using HttpErrorCallback = std::function<void(const WirelessErrorInfo& error)>; 

struct HttpRequest {
    std::string path;
    std::string method;
    std::string contentType;
    std::string payload;
    HttpSuccessCallback onSuccess;
    HttpErrorCallback onError;
    bool inProgress;
    unsigned long lastAttemptTime;
    int retryCount;
    std::string responseData;

    /**
     * `contentType` is the Content-Type header the ESP32 driver sends on POST and
     * PUT; GET ignores it.
     */
    HttpRequest(const std::string& path, const std::string& method, const std::string& contentType, const std::string& payload, HttpSuccessCallback onSuccess, HttpErrorCallback onError)
        : path(path)
        , method(method)
        , contentType(contentType)
        , payload(payload)
        , onSuccess(std::move(onSuccess))
        , onError(std::move(onError))
        , inProgress(false)
        , lastAttemptTime(0)
        , retryCount(0)
        , responseData("") {}
};