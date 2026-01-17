#pragma once

#include <string>
#include <functional>
#include "wireless-types.hpp"

class HttpClientInterface {
public:
    virtual ~HttpClientInterface() = default;

    virtual void setWifiConfig(WifiConfig* config) = 0;
    virtual bool isConnected() = 0;
    virtual bool queueRequest(HttpRequest& request) = 0;
    virtual void disconnect() = 0;
    virtual void updateConfig(WifiConfig* config) = 0;
    virtual void retryConnection() = 0;
    virtual uint8_t* getMacAddress() = 0;
};