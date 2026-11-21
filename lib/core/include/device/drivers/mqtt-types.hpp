#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct MqttConfig {
    std::string brokerHost;
    int brokerPort = 1883;
    std::string username;
    std::string password;
    std::string clientId;
    std::string lwtTopic;
    /** Empty payload is the fabric offline LWT convention. */
    std::string lwtPayload;
};

using MqttMessageHandler =
    std::function<void(const std::string& topic, const uint8_t* data, size_t length)>;
