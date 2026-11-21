#pragma once

#include <string>

#include <ArduinoJson.h>

#include "device/drivers/logger.hpp"
#include "device/drivers/mqtt-interface.hpp"
#include "wireless/page-manager.hpp"

static const char* DNN_MQTT_TAG = "DnnMqtt";

/**
 * AlleycatTV-style fabric client for Data Node Nexus (`mc/dnn`).
 * Subscribes device + all + membership; broadcast group after BGC assign.
 */
class DnnMqttClient {
public:
    void initialize(MQTTInterface* mqtt, const std::string& deviceId, PageManager* pageManager) {
        this->mqtt = mqtt;
        this->deviceId = deviceId;
        this->pageManager = pageManager;
        statusTopic = std::string("mc/dnn/status/") + deviceId;
        topicDevice = std::string("mc/dnn/cmd/device/") + deviceId + "/#";
        topicAll = "mc/dnn/cmd/all/#";
        topicMembership = std::string("mc/dnn/cmd/device/") + deviceId + "/membership";
    }

    void start(MqttConfig cfg) {
        if (mqtt == nullptr) {
            return;
        }
        cfg.lwtTopic = statusTopic;
        cfg.lwtPayload = "";
        if (cfg.clientId.empty()) {
            cfg.clientId = std::string("fdn-") + deviceId;
        }
        mqtt->configure(cfg);
        mqtt->setMessageHandler(
            [this](const std::string& topic, const uint8_t* data, size_t length) {
                onMessage(topic, data, length);
            });
        mqtt->connect();
    }

    /** (Re)subscribe after MQTT_EVENT_CONNECTED. */
    void onBrokerConnected() {
        if (mqtt == nullptr) {
            return;
        }
        mqtt->subscribe(topicDevice, 1);
        mqtt->subscribe(topicAll, 1);
        mqtt->subscribe(topicMembership, 1);
        mqtt->publish(statusTopic, "online", 1, true);
        if (!broadcastGroupId.empty()) {
            subscribeBroadcastGroup(broadcastGroupId);
        }
        LOG_I(DNN_MQTT_TAG, "Subscribed as %s", deviceId.c_str());
    }

    void loop() {
        if (mqtt != nullptr) {
            mqtt->loop();
        }
    }

    const std::string& getDeviceId() const { return deviceId; }
    const std::string& getBroadcastGroupId() const { return broadcastGroupId; }

private:
    void onMessage(const std::string& topic, const uint8_t* data, size_t length) {
        if (topic == topicMembership) {
            handleMembership(data, length);
            return;
        }

        const std::string action = actionFromTopic(topic);
        if (action != "page") {
            LOG_D(DNN_MQTT_TAG, "Ignore action '%s' on %s", action.c_str(), topic.c_str());
            return;
        }
        if (pageManager != nullptr) {
            pageManager->ingestMqttPayload(data, length);
        }
    }

    void handleMembership(const uint8_t* data, size_t length) {
        std::string groupId;
        if (data != nullptr && length > 0) {
            JsonDocument doc;
            if (deserializeJson(doc, reinterpret_cast<const char*>(data), length) ==
                DeserializationError::Ok) {
                if (!doc["broadcast_group_id"].isNull()) {
                    groupId = doc["broadcast_group_id"] | "";
                }
            }
        }
        applyBroadcastGroup(groupId);
    }

    void applyBroadcastGroup(const std::string& groupId) {
        if (groupId == broadcastGroupId) {
            return;
        }
        if (!topicBroadcast.empty() && mqtt != nullptr) {
            mqtt->unsubscribe(topicBroadcast);
        }
        broadcastGroupId = groupId;
        topicBroadcast.clear();
        if (!broadcastGroupId.empty()) {
            subscribeBroadcastGroup(broadcastGroupId);
        }
        LOG_I(DNN_MQTT_TAG, "Broadcast group -> '%s'", broadcastGroupId.c_str());
    }

    void subscribeBroadcastGroup(const std::string& groupId) {
        topicBroadcast = std::string("mc/dnn/cmd/broadcast/") + groupId + "/#";
        if (mqtt != nullptr && mqtt->isConnected()) {
            mqtt->subscribe(topicBroadcast, 1);
        }
    }

    static std::string actionFromTopic(const std::string& topic) {
        const auto pos = topic.find_last_of('/');
        if (pos == std::string::npos || pos + 1 >= topic.size()) {
            return {};
        }
        return topic.substr(pos + 1);
    }

    MQTTInterface* mqtt = nullptr;
    PageManager* pageManager = nullptr;
    std::string deviceId;
    std::string statusTopic;
    std::string topicDevice;
    std::string topicAll;
    std::string topicMembership;
    std::string topicBroadcast;
    std::string broadcastGroupId;
};
