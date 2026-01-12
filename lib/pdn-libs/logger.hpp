#pragma once

#include <cstdarg>

enum class LogLevel {
    ERROR,
    WARN,
    INFO,
    DEBUG,
    VERBOSE
};

/**
 * Platform-agnostic logging interface.
 * Abstracts the underlying logging framework (ESP_LOG, printf, etc.)
 */
class LoggerInterface {
public:
    virtual ~LoggerInterface() = default;
    
    /**
     * Log a message at the specified level.
     * @param level Log level
     * @param tag Tag/component name (e.g., "HttpClient")
     * @param format Printf-style format string
     * @param ... Variable arguments for format string
     */
    virtual void log(LogLevel level, const char* tag, const char* format, ...) = 0;
    
    /**
     * Log with va_list (for wrapping by other functions).
     */
    virtual void vlog(LogLevel level, const char* tag, const char* format, va_list args) = 0;
};

/**
 * Global logger instance pointer.
 * Must be set before any logging occurs.
 */
extern LoggerInterface* g_logger;

/**
 * Convenience macros for logging (similar to ESP_LOG style).
 * Usage: LOG_E("TAG", "Error: %d", errorCode);
 */
#define LOG_E(tag, format, ...) \
    do { if (g_logger) g_logger->log(LogLevel::ERROR, tag, format, ##__VA_ARGS__); } while(0)

#define LOG_W(tag, format, ...) \
    do { if (g_logger) g_logger->log(LogLevel::WARN, tag, format, ##__VA_ARGS__); } while(0)

#define LOG_I(tag, format, ...) \
    do { if (g_logger) g_logger->log(LogLevel::INFO, tag, format, ##__VA_ARGS__); } while(0)

#define LOG_D(tag, format, ...) \
    do { if (g_logger) g_logger->log(LogLevel::DEBUG, tag, format, ##__VA_ARGS__); } while(0)

#define LOG_V(tag, format, ...) \
    do { if (g_logger) g_logger->log(LogLevel::VERBOSE, tag, format, ##__VA_ARGS__); } while(0)
