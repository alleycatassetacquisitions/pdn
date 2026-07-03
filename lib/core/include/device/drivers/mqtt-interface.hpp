#pragma once

#include "mqtt-types.hpp"
#include <string>

class MQTTInterface {
public:
    virtual ~MQTTInterface() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual void publish(const Topic topic, const MQTTPayload& payload) = 0;
    virtual void subscribe(const Topic topic) = 0;
    virtual void unsubscribe(const Topic topic) = 0;
    virtual void onMessage(const Topic topic, const MQTTPayload& payload) = 0;
    virtual void onConnect() = 0;
    virtual void onDisconnect() = 0;
};