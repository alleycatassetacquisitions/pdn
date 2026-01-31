#pragma once

#include <string>
#include <functional>
#include "wireless/wireless-types.hpp"

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
    
    /**
     * Enable ESP-NOW mode: disconnect from AP and set WiFi to a fixed channel.
     * This allows reliable ESP-NOW communication between devices.
     * @param channel The WiFi channel to use (typically 6)
     */
    virtual void enablePeerCommsMode(uint8_t channel) = 0;
    
    /**
     * Enable HTTP mode: connect to WiFi AP for HTTP requests.
     * @return true if connection was successful or is in progress
     */
    virtual bool enableHttpMode() = 0;
};