#pragma once

#include <Arduino.h>
#include <functional>
#include <optional>

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
    String message;
    bool willRetry;
};

/**
 * Configuration struct for WiFi credentials and settings
 */
struct WifiConfig {
    String ssid;
    String password;
    String baseUrl;
    
    WifiConfig() : ssid(""), password(""), baseUrl("") {}
    
    WifiConfig(const String& ssid, const String& password, const String& baseUrl)
        : ssid(ssid), password(password), baseUrl(baseUrl) {}
};

// Callback definitions
using HttpSuccessCallback = std::function<void(const String& jsonResponse)>;
using HttpErrorCallback = std::function<void(const WirelessErrorInfo& error)>; 