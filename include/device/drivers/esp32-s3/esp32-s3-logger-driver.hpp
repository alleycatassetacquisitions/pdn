#pragma once

#include "device/drivers/driver-interface.hpp"
#include <esp_log.h>
#include <cstring>

/**
 * ESP32-S3 implementation of LoggerInterface.
 * Wraps ESP-IDF's esp_log functions with source location tracking.
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
    
    void vlog(LogLevel level, const char* tag, const char* file, int line, const char* format, va_list args) override {
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        // Extract just the filename from the full path
        const char* filename = file;
        const char* lastSlash = strrchr(file, '/');
        const char* lastBackslash = strrchr(file, '\\');
        if (lastSlash) filename = lastSlash + 1;
        if (lastBackslash && lastBackslash > filename) filename = lastBackslash + 1;
        
        // Map our LogLevel to ESP-IDF log level and get level character
        esp_log_level_t espLevel;
        char levelChar;
        switch (level) {
            case LogLevel::ERROR:   espLevel = ESP_LOG_ERROR;   levelChar = 'E'; break;
            case LogLevel::WARN:    espLevel = ESP_LOG_WARN;    levelChar = 'W'; break;
            case LogLevel::INFO:    espLevel = ESP_LOG_INFO;    levelChar = 'I'; break;
            case LogLevel::DEBUG:   espLevel = ESP_LOG_DEBUG;   levelChar = 'D'; break;
            case LogLevel::VERBOSE: espLevel = ESP_LOG_VERBOSE; levelChar = 'V'; break;
            default:                espLevel = ESP_LOG_INFO;    levelChar = '?'; break;
        }
        
        // Use esp_log_write with custom format that includes our source location
        // Format: [timestamp][level][tag][file:line] message
        esp_log_write(espLevel, tag, "[%6lu][%c][%s][%s:%d] %s\n", 
                      (unsigned long)(esp_log_timestamp()), levelChar, tag, filename, line, buffer);
    }
};
