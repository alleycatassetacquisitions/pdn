#pragma once

#include "logger.hpp"
#include <esp_log.h>

/**
 * ESP32-S3 implementation of LoggerInterface.
 * Wraps ESP-IDF's esp_log functions.
 */
class Esp32S3Logger : public LoggerInterface {
public:
    void log(LogLevel level, const char* tag, const char* format, ...) override {
        va_list args;
        va_start(args, format);
        vlog(level, tag, format, args);
        va_end(args);
    }
    
    void vlog(LogLevel level, const char* tag, const char* format, va_list args) override {
        esp_log_level_t esp_level;
        
        switch (level) {
            case LogLevel::ERROR:   esp_level = ESP_LOG_ERROR; break;
            case LogLevel::WARN:    esp_level = ESP_LOG_WARN; break;
            case LogLevel::INFO:    esp_level = ESP_LOG_INFO; break;
            case LogLevel::DEBUG:   esp_level = ESP_LOG_DEBUG; break;
            case LogLevel::VERBOSE: esp_level = ESP_LOG_VERBOSE; break;
            default: return;
        }
        
        esp_log_writev(esp_level, tag, format, args);
    }
};
