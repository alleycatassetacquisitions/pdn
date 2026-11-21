#pragma once

#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "mqtt_client.h"

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/mqtt-interface.hpp"

static const char* MQTT_DRV_TAG = "MQTT";

/**
 * ESP-IDF mqtt client wrapped as a driver. Messages are queued in the MQTT
 * event task and drained on loop() so app code never runs on the MQTT task.
 */
class Esp32S3MQTTDriver : public MQTTDriverInterface {
public:
    explicit Esp32S3MQTTDriver(const std::string& name) : MQTTDriverInterface(name) {}

    ~Esp32S3MQTTDriver() override {
        disconnect();
    }

    int initialize() override { return 0; }

    void exec() override { loop(); }

    void configure(const MqttConfig& config) override {
        mqttConfig = config;
        configured = true;
    }

    void connect() override {
        if (!configured) {
            LOG_E(MQTT_DRV_TAG, "connect() without configure()");
            return;
        }
        if (client != nullptr) {
            esp_mqtt_client_start(client);
            return;
        }

        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = nullptr;
        brokerUri = "mqtt://" + mqttConfig.brokerHost + ":" + std::to_string(mqttConfig.brokerPort);
        cfg.broker.address.uri = brokerUri.c_str();
        cfg.credentials.client_id = mqttConfig.clientId.c_str();
        if (!mqttConfig.username.empty()) {
            cfg.credentials.username = mqttConfig.username.c_str();
            cfg.credentials.authentication.password = mqttConfig.password.c_str();
        }
        if (!mqttConfig.lwtTopic.empty()) {
            cfg.session.last_will.topic = mqttConfig.lwtTopic.c_str();
            cfg.session.last_will.msg = mqttConfig.lwtPayload.c_str();
            cfg.session.last_will.msg_len = static_cast<int>(mqttConfig.lwtPayload.size());
            cfg.session.last_will.qos = 1;
            cfg.session.last_will.retain = true;
        }
        cfg.session.keepalive = 60;
        cfg.network.reconnect_timeout_ms = 5000;

        client = esp_mqtt_client_init(&cfg);
        if (client == nullptr) {
            LOG_E(MQTT_DRV_TAG, "esp_mqtt_client_init failed");
            return;
        }
        esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, &Esp32S3MQTTDriver::onMqttEvent,
                                       this);
        esp_err_t err = esp_mqtt_client_start(client);
        if (err != ESP_OK) {
            LOG_E(MQTT_DRV_TAG, "esp_mqtt_client_start: %s", esp_err_to_name(err));
        }
    }

    void disconnect() override {
        if (client == nullptr) {
            return;
        }
        if (connected) {
            if (!mqttConfig.lwtTopic.empty()) {
                esp_mqtt_client_publish(client, mqttConfig.lwtTopic.c_str(), "", 0, 1, 1);
            }
        }
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = nullptr;
        connected = false;
    }

    bool isConnected() const override { return connected; }

    void publish(const std::string& topic, const std::string& payload, int qos,
                 bool retain) override {
        if (client == nullptr) {
            return;
        }
        esp_mqtt_client_publish(client, topic.c_str(), payload.c_str(),
                                static_cast<int>(payload.size()), qos, retain ? 1 : 0);
    }

    void subscribe(const std::string& topic, int qos = 1) override {
        if (client == nullptr) {
            return;
        }
        esp_mqtt_client_subscribe(client, topic.c_str(), qos);
        LOG_I(MQTT_DRV_TAG, "Subscribe %s", topic.c_str());
    }

    void unsubscribe(const std::string& topic) override {
        if (client == nullptr) {
            return;
        }
        esp_mqtt_client_unsubscribe(client, topic.c_str());
    }

    void setMessageHandler(MqttMessageHandler handler) override { messageHandler = std::move(handler); }

    void loop() override {
        std::queue<QueuedMessage> pending;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            pending.swap(inbound);
        }
        while (!pending.empty()) {
            auto& msg = pending.front();
            if (messageHandler) {
                messageHandler(msg.topic, msg.payload.data(), msg.payload.size());
            }
            pending.pop();
        }
    }

private:
    struct QueuedMessage {
        std::string topic;
        std::vector<uint8_t> payload;
    };

    static void onMqttEvent(void* handlerArgs, esp_event_base_t /*base*/, int32_t eventId,
                            void* eventData) {
        auto* self = static_cast<Esp32S3MQTTDriver*>(handlerArgs);
        auto* event = static_cast<esp_mqtt_event_handle_t>(eventData);
        switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
            case MQTT_EVENT_CONNECTED:
                LOG_I(MQTT_DRV_TAG, "Connected");
                self->connected = true;
                if (!self->mqttConfig.lwtTopic.empty()) {
                    self->publish(self->mqttConfig.lwtTopic, "online", 1, true);
                }
                if (self->onConnected) {
                    self->onConnected();
                }
                break;
            case MQTT_EVENT_DISCONNECTED:
                LOG_W(MQTT_DRV_TAG, "Disconnected");
                self->connected = false;
                break;
            case MQTT_EVENT_DATA: {
                QueuedMessage msg;
                if (event->topic && event->topic_len > 0) {
                    msg.topic.assign(event->topic, static_cast<size_t>(event->topic_len));
                }
                if (event->data && event->data_len > 0) {
                    msg.payload.assign(event->data, event->data + event->data_len);
                }
                std::lock_guard<std::mutex> lock(self->queueMutex);
                if (self->inbound.size() < 16) {
                    self->inbound.push(std::move(msg));
                }
                break;
            }
            default:
                break;
        }
    }

public:
    /** Invoked on the MQTT task when the broker accepts the session; used to (re)subscribe. */
    std::function<void()> onConnected;

private:
    MqttConfig mqttConfig;
    std::string brokerUri;
    esp_mqtt_client_handle_t client = nullptr;
    bool configured = false;
    bool connected = false;
    MqttMessageHandler messageHandler;
    std::mutex queueMutex;
    std::queue<QueuedMessage> inbound;
};
