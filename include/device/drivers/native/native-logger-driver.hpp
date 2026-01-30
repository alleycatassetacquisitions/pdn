#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>

class NativeLoggerDriver : public LoggerDriverInterface {
public:
    explicit NativeLoggerDriver(const std::string& name) : LoggerDriverInterface(name) {}

    ~NativeLoggerDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {}

    void vlog(LogLevel level, const char* tag, const char* file, int line, const char* format, va_list args) override {
        // Get timestamp in milliseconds since start
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime_).count();

        // Level prefix
        const char* levelStr;
        const char* colorCode;
        switch (level) {
            case LogLevel::ERROR:   levelStr = "E"; colorCode = "\033[31m"; break; // Red
            case LogLevel::WARN:    levelStr = "W"; colorCode = "\033[33m"; break; // Yellow
            case LogLevel::INFO:    levelStr = "I"; colorCode = "\033[32m"; break; // Green
            case LogLevel::DEBUG:   levelStr = "D"; colorCode = "\033[36m"; break; // Cyan
            case LogLevel::VERBOSE: levelStr = "V"; colorCode = "\033[37m"; break; // White
            default:                levelStr = "?"; colorCode = "\033[0m";  break;
        }

        // Extract just the filename from the full path
        const char* filename = file;
        const char* lastSlash = strrchr(file, '/');
        const char* lastBackslash = strrchr(file, '\\');
        if (lastSlash) filename = lastSlash + 1;
        if (lastBackslash && lastBackslash > filename) filename = lastBackslash + 1;

        // Print timestamp, level, tag, and source location
        printf("%s[%6lld][%s][%s:%d][%s] ", colorCode, (long long)elapsed, levelStr, filename, line, tag);
        
        // Print the formatted message
        vprintf(format, args);
        
        // Reset color and newline
        printf("\033[0m\n");
        fflush(stdout);
    }

private:
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
};
