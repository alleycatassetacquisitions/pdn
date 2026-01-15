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
 * 
 * Note: Only vlog() is virtual to avoid issues with variadic functions
 * and multiple inheritance (which prevents thunk generation).
 */
class LoggerInterface {
public:
    virtual ~LoggerInterface() = default;
    
    /**
     * Log with va_list.
     * This is the core method that implementations must provide.
     */
    virtual void vlog(LogLevel level, const char* tag, const char* format, va_list args) = 0;
};

/**
 * Global logger instance pointer.
 * Must be set before any logging occurs.
 */
extern LoggerInterface* g_logger;

/**
 * Helper function to wrap variadic arguments and call vlog.
 * This is needed because we can't have variadic virtual functions with multiple inheritance.
 */
inline void log_helper(LogLevel level, const char* tag, const char* format, ...) {
    if (g_logger) {
        va_list args;
        va_start(args, format);
        g_logger->vlog(level, tag, format, args);
        va_end(args);
    }
}

/**
 * Convenience macros for logging (similar to ESP_LOG style).
 * Usage: LOG_E("TAG", "Error: %d", errorCode);
 */
#define LOG_E(tag, format, ...) \
    log_helper(LogLevel::ERROR, tag, format, ##__VA_ARGS__)

#define LOG_W(tag, format, ...) \
    log_helper(LogLevel::WARN, tag, format, ##__VA_ARGS__)

#define LOG_I(tag, format, ...) \
    log_helper(LogLevel::INFO, tag, format, ##__VA_ARGS__)

#define LOG_D(tag, format, ...) \
    log_helper(LogLevel::DEBUG, tag, format, ##__VA_ARGS__)

#define LOG_V(tag, format, ...) \
    log_helper(LogLevel::VERBOSE, tag, format, ##__VA_ARGS__)
