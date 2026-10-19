#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include "wireless/wireless-types.hpp"

enum class HttpClientState {
    CONNECTED,
    DISCONNECTED,
};

class HttpClientInterface {
public:
    virtual ~HttpClientInterface() = default;

    virtual void setWifiConfig(WifiConfig* config) = 0;
    virtual bool isConnected() = 0;
    /** Takes ownership of @p request; a large payload is moved, not copied. */
    virtual bool queueRequest(HttpRequest&& request) = 0;
    virtual void disconnect() = 0;
    virtual void updateConfig(WifiConfig* config) = 0;
    virtual void retryConnection() = 0;
    virtual uint8_t* getMacAddress() = 0;
    virtual void setHttpClientState(HttpClientState state) = 0;
    virtual HttpClientState getHttpClientState() = 0;
    /** True while a queued request has not had its callback. */
    virtual bool hasQueuedRequests() = 0;
};