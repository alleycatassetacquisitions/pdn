#pragma once

#include <string>

#include <ArduinoJson.h>

#include "device/drivers/logger.hpp"
#include "device/drivers/mqtt-interface.hpp"
#include "wireless/page-relay.hpp"

static const char* DNN_MQTT_TAG = "DnnMqtt";

/**
 * AlleycatTV-style fabric client for Data Node Nexus (`mc/dnn`).
 * Subscribes device + all + membership; broadcast group after BGC assign.
 * Every handler runs on the app task (the MQTT driver defers them to its loop()).
 */
class DnnMqttClient {
public:
    void initialize(MQTTInterface* mqtt, const std::string& deviceId, PageRelay* pageRelay) {
        this->mqtt = mqtt;
        this->deviceId = deviceId;
        this->pageRelay = pageRelay;
        statusTopic = std::string("mc/dnn/status/") + deviceId;
        topicDevice = std::string("mc/dnn/cmd/device/") + deviceId + "/#";
        topicAll = "mc/dnn/cmd/all/#";
        topicMembership = std::string("mc/dnn/cmd/device/") + deviceId + "/membership";
    }

    /** `cfg` carries broker and login; topics, will, and client id are set here. */
    void start(MqttConfig cfg) {
        if (mqtt == nullptr) {
            return;
        }
        cfg.lwtTopic = statusTopic;
        cfg.clientId = std::string("fdn-") + deviceId;
        mqtt->configure(cfg);
        mqtt->setConnectedHandler([this]() { onBrokerConnected(); });
        mqtt->setMessageHandler(
            [this](const std::string& topic, const uint8_t* data, size_t length) {
                onMessage(topic, data, length);
            });
        mqtt->connect();
    }

private:
    /** (Re)subscribe and announce online after every broker session start. */
    void onBrokerConnected() {
        mqtt->subscribe(topicDevice, 1);
        mqtt->subscribe(topicAll, 1);
        mqtt->subscribe(topicMembership, 1);
        mqtt->publish(statusTopic, "online", 1, true);
        if (!broadcastGroupId.empty()) {
            subscribeBroadcastGroup(broadcastGroupId);
        }
        LOG_I(DNN_MQTT_TAG, "Subscribed as %s", deviceId.c_str());
    }

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
        if (pageRelay != nullptr) {
            pageRelay->ingest(data, length);
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
        // The id becomes a topic level; a wildcard or separator would widen the subscription.
        if (groupId.find_first_of("+#/") != std::string::npos) {
            LOG_W(DNN_MQTT_TAG, "Reject broadcast group id '%s'", groupId.c_str());
            return;
        }
        applyBroadcastGroup(groupId);
    }

    void applyBroadcastGroup(const std::string& groupId) {
        if (groupId == broadcastGroupId) {
            return;
        }
        if (!topicBroadcast.empty()) {
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
        if (mqtt->isConnected()) {
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
    PageRelay* pageRelay = nullptr;
    std::string deviceId;
    std::string statusTopic;
    std::string topicDevice;
    std::string topicAll;
    std::string topicMembership;
    std::string topicBroadcast;
    std::string broadcastGroupId;
};
