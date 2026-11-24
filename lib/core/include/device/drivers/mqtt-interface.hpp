#pragma once

#include "mqtt-types.hpp"
#include <string>

class MQTTInterface {
public:
    virtual ~MQTTInterface() = default;

    virtual void configure(const MqttConfig& config) = 0;
    virtual void connect() = 0;
    virtual bool isConnected() const = 0;
    virtual void publish(const std::string& topic, const std::string& payload, int qos,
                         bool retain) = 0;
    virtual void subscribe(const std::string& topic, int qos = 1) = 0;
    virtual void unsubscribe(const std::string& topic) = 0;
    virtual void setMessageHandler(MqttMessageHandler handler) = 0;
    /** Called each time the broker accepts the session (first connect and every reconnect). */
    virtual void setConnectedHandler(MqttConnectedHandler handler) = 0;
    /** Runs the connected and message handlers on the app task. */
    virtual void loop() = 0;
};
