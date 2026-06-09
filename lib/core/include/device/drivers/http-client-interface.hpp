#pragma once

#include <cstdint>
#include "wireless/wireless-types.hpp"

// WIFI_ENGAGED = the WiFi excursion is requested/active (connecting, up, or
// given up), not that AP association succeeded; isConnected() reports that.
enum class HttpClientState {
    WIFI_ENGAGED,
    IDLE,
};

class HttpClientInterface {
public:
    virtual ~HttpClientInterface() = default;

    virtual bool isConnected() = 0;
    virtual bool queueRequest(HttpRequest& request) = 0;
    virtual void disconnect() = 0;
    virtual uint8_t* getMacAddress() = 0;
    virtual void setHttpClientState(HttpClientState state) = 0;
    virtual HttpClientState getHttpClientState() = 0;
};