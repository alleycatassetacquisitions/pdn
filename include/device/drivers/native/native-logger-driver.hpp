#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>
#include <deque>

struct LogEntry {
    LogLevel level;
    std::string tag;
    std::string file;
    int line;
    std::string message;
    long long timestamp;
};

class NativeLoggerDriver : public LoggerDriverInterface {
public:
    explicit NativeLoggerDriver(const std::string& name) : LoggerDriverInterface(name) {}

    ~NativeLoggerDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {}

    // Control whether logs are printed to stdout
    void setSuppressOutput(bool suppress) {
        suppressOutput_ = suppress;
    }
    
    bool isSuppressed() const {
        return suppressOutput_;
    }
    
    // Get buffered logs (returns copy and clears buffer)
    std::vector<LogEntry> getAndClearLogs() {
        std::vector<LogEntry> result(logBuffer_.begin(), logBuffer_.end());
        logBuffer_.clear();
        return result;
    }
    
    // Get recent logs without clearing (for display)
    const std::deque<LogEntry>& getRecentLogs() const {
        return logBuffer_;
    }
    
    // Set max buffer size
    void setMaxBufferSize(size_t size) {
        maxBufferSize_ = size;
        while (logBuffer_.size() > maxBufferSize_) {
            logBuffer_.pop_front();
        }
    }

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

        // Format the message
        char msgBuffer[1024];
        vsnprintf(msgBuffer, sizeof(msgBuffer), format, args);
        
        // Buffer the log entry
        LogEntry entry;
        entry.level = level;
        entry.tag = tag;
        entry.file = filename;
        entry.line = line;
        entry.message = msgBuffer;
        entry.timestamp = elapsed;
        
        logBuffer_.push_back(entry);
        while (logBuffer_.size() > maxBufferSize_) {
            logBuffer_.pop_front();
        }

        // Only print if not suppressed
        if (!suppressOutput_) {
            printf("%s[%6lld][%s][%s:%d][%s] %s\033[0m\n", 
                   colorCode, (long long)elapsed, levelStr, filename, line, tag, msgBuffer);
            fflush(stdout);
        }
    }

private:
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
    bool suppressOutput_ = false;
    std::deque<LogEntry> logBuffer_;
    size_t maxBufferSize_ = 100;  // Keep last 100 log entries
};
