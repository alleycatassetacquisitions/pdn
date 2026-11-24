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
    /** Retained last-will topic; the will payload is empty (fabric "offline" convention). */
    std::string lwtTopic;
};

using MqttMessageHandler =
    std::function<void(const std::string& topic, const uint8_t* data, size_t length)>;
using MqttConnectedHandler = std::function<void()>;
