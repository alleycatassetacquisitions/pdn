#pragma once

#include <functional>
#include <optional>
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
    // The FDN refused to send to an address that is not https://, or the server
    // failed TLS verification (CA bundle or host name); a retry meets the same.
    SERVER_NOT_TRUSTED
};

struct WirelessErrorInfo {
    WirelessError code;
    std::string message;
    // The status the server answered with, when the failure is an HTTP error
    // response; 0 when no response arrived or the failure was local.
    int httpStatus = 0;
};

/** Whether @p baseUrl is an https:// address, the only kind the FDN sends to. */
inline bool isHttpsAddress(const std::string& baseUrl) { return baseUrl.rfind("https://", 0) == 0; }

/**
 * The failure an HTTP @p status means, or none for a 2xx. A 3xx fails too:
 * redirects are not followed, since one could lead away from https.
 */
inline std::optional<WirelessErrorInfo> httpStatusFailure(int status) {
    if (status >= 200 && status < 300) return std::nullopt;
    const WirelessError code =
    status >= 500 ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE;
    return WirelessErrorInfo{code, "HTTP Error: " + std::to_string(status), status};
}

/**
 * Configuration struct for WiFi credentials and settings
 */
struct WifiConfig {
    std::string ssid;
    std::string password;
    std::string baseUrl;
    // The NTP server SNTP sets the clock from before the first TLS connection.
    std::string ntpServer;

    /** Holds the build-time Wi-Fi credentials, server address and NTP server. */
    WifiConfig(const std::string& ssid, const std::string& password, const std::string& baseUrl,
               const std::string& ntpServer)
        : ssid(ssid)
        , password(password)
        , baseUrl(baseUrl)
        , ntpServer(ntpServer) {}
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
    // How much of `path` a log may show. A request whose path carries a band UID
    // cuts it off here, and a log then shows neither the rest nor the server's
    // error text, which may echo it.
    size_t loggablePathLength = std::string::npos;

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

    /** @return `path` as a log may show it. */
    std::string getLoggablePath() const { return path.substr(0, loggablePathLength); }

    /** @return true when part of `path` must never be logged. */
    bool hasPrivatePath() const { return loggablePathLength < path.size(); }
};