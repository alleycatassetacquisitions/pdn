#pragma once

#include "device/drivers/driver-interface.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
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

    /** Entries are always buffered; this stops them being printed as well. */
    void setSuppressOutput(bool suppress) { suppressOutput = suppress; }

    /** The buffered entries, oldest first. */
    const std::deque<LogEntry>& getRecentLogs() const { return logBuffer; }

    void vlog(LogLevel level, const char* tag, const char* file, int line, const char* format, va_list args) override {
        // Get timestamp in milliseconds since start
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();

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

        logBuffer.push_back(entry);
        while (logBuffer.size() > MAX_BUFFER_SIZE) {
            logBuffer.pop_front();
        }

        // Only print if not suppressed
        if (!suppressOutput) {
            FILE* out = stderr;
            static FILE* debugFile = nullptr;
            if (debugFile == nullptr) {
                const char* path = getenv("PDN_CLI_LOG_FILE");
                if (path && *path) debugFile = fopen(path, "w");
            }
            if (debugFile) out = debugFile;
            fprintf(out, "[%6lld][%s][%s:%d][%s] %s\n",
                    (long long)elapsed, levelStr, filename, line, tag, msgBuffer);
            fflush(out);
        }
    }

private:
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    bool suppressOutput = false;
    std::deque<LogEntry> logBuffer;
    static constexpr size_t MAX_BUFFER_SIZE = 100;
};
