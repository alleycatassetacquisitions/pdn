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
};

// Callback definitions
using HttpSuccessCallback = std::function<void(const String& jsonResponse)>;
using HttpErrorCallback = std::function<void(const WirelessErrorInfo& error)>; 