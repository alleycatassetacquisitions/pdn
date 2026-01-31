#pragma once

#include "device/drivers/driver-interface.hpp"
#include <esp_log.h>

/**
 * ESP32-S3 implementation of LoggerInterface.
 * Wraps ESP-IDF's esp_log functions.
 */
class Esp32S3Logger : public LoggerDriverInterface {
public:
    Esp32S3Logger(std::string name) : LoggerDriverInterface(name) {
    }

    ~Esp32S3Logger() override {
    }

    int initialize() override {
        return 0;   
    }

    void exec() override {
    }
    
    void vlog(LogLevel level, const char* tag, const char* format, va_list args) override {
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        switch (level) {
            case LogLevel::ERROR:   ESP_LOGE(tag, "%s", buffer); break;
            case LogLevel::WARN:    ESP_LOGW(tag, "%s", buffer); break;
            case LogLevel::INFO:    ESP_LOGI(tag, "%s", buffer); break;
            case LogLevel::DEBUG:   ESP_LOGD(tag, "%s", buffer); break;
            case LogLevel::VERBOSE: ESP_LOGV(tag, "%s", buffer); break;
        }
    }
};
