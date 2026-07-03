#pragma once

#include "mqtt_client.h"
#include "device/drivers/driver-interface.hpp"

class Esp32S3MQTTDriver : public MQTTDriverInterface {
    public:
    Esp32S3MQTTDriver(const std::string& name) : MQTTDriverInterface(name) {}
    ~Esp32S3MQTTDriver() override = default;
    int initialize() override {
        // TODO: Implement
        return 0;
    }
    void exec() override {
        // TODO: Implement
    }
    void onMessage(const Topic topic, const MQTTPayload& payload) override {
        // TODO: Implement
    }
    void onConnect() override {
        // TODO: Implement
    }
    void onDisconnect() override {
        // TODO: Implement
    }
};