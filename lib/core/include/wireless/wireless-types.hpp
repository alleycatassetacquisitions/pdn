#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

enum class WirelessError {
    TIMEOUT,
    CONNECTION_FAILED,
    INVALID_RESPONSE,
    SERVER_ERROR,
    WIFI_NOT_CONNECTED,
    INVALID_STATE,
    // The HTTPS server presented a key other than the pinned one, or no key is
    // pinned; retrying would meet the same key.
    SERVER_NOT_TRUSTED
};

struct WirelessErrorInfo {
    WirelessError code;
    std::string message;
    // The status the server answered with, when the failure is an HTTP error
    // response; 0 when no response arrived or the failure was local.
    int httpStatus = 0;
};

// Largest legitimate reply is a fully rejected match batch (~9.8 KB; matches-api.hpp checks
// it fits); 16 KiB leaves margin while capping what a bad server can make the device buffer.
constexpr size_t MAX_HTTP_RESPONSE_BYTES = 16 * 1024;

/**
 * Configuration struct for WiFi credentials and settings
 */
struct WifiConfig {
    std::string ssid;
    std::string password;
    std::string baseUrl;
    // SHA-256 of the server's DER SubjectPublicKeyInfo as 64 hex digits; only an
    // HTTPS server presenting that key is trusted. Empty or malformed, no HTTPS
    // server is.
    std::string serverKeySha256;

    /** An empty configuration: no network, no server, no pin. */
    WifiConfig()
        : ssid("")
        , password("")
        , baseUrl("")
        , serverKeySha256("") {}

    /** A configuration for @p ssid and @p baseUrl; with no pin no HTTPS server is trusted. */
    WifiConfig(const std::string& ssid, const std::string& password, const std::string& baseUrl,
               const std::string& serverKeySha256 = "")
        : ssid(ssid)
        , password(password)
        , baseUrl(baseUrl)
        , serverKeySha256(serverKeySha256) {}
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
    std::vector<std::pair<std::string, std::string>> requestHeaders;

    /**
     * `contentType` is the Content-Type header the ESP32 driver sends on POST and
     * PUT; GET ignores it.
     */
    HttpRequest(std::string path, std::string method, std::string contentType, std::string payload, HttpSuccessCallback onSuccess, HttpErrorCallback onError)
        : path(std::move(path))
        , method(std::move(method))
        , contentType(std::move(contentType))
        , payload(std::move(payload))
        , onSuccess(std::move(onSuccess))
        , onError(std::move(onError))
        , inProgress(false)
        , lastAttemptTime(0)
        , retryCount(0)
        , responseData("") {}
};