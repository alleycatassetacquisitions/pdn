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
     * Log with va_list and source location.
     * This is the core method that implementations must provide.
     * @param level Log severity level
     * @param tag Category tag for the log message
     * @param file Source file name (__FILE__)
     * @param line Source line number (__LINE__)
     * @param format Printf-style format string
     * @param args Variable arguments
     */
    virtual void vlog(LogLevel level, const char* tag, const char* file, int line, const char* format, va_list args) = 0;
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
inline void log_helper(LogLevel level, const char* tag, const char* file, int line, const char* format, ...) {
    if (g_logger) {
        va_list args;
        va_start(args, format);
        g_logger->vlog(level, tag, file, line, format, args);
        va_end(args);
    }
}

/**
 * Compile-time log level filter.
 *
 * CORE_DEBUG_LEVEL is set per-environment in platformio.ini:
 *   0 = silent   1 = error   2 = warn   3 = info   4 = debug   5 = verbose
 *
 * Macros below the active level expand to nothing, so their arguments
 * (e.g. MacToString(), string formatting) are never evaluated.
 */
#ifndef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL 3  // Default: info and above
#endif

#if CORE_DEBUG_LEVEL >= 1
#define LOG_E(tag, format, ...) \
    log_helper(LogLevel::ERROR, tag, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define LOG_E(tag, format, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= 2
#define LOG_W(tag, format, ...) \
    log_helper(LogLevel::WARN, tag, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define LOG_W(tag, format, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= 3
#define LOG_I(tag, format, ...) \
    log_helper(LogLevel::INFO, tag, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define LOG_I(tag, format, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= 4
#define LOG_D(tag, format, ...) \
    log_helper(LogLevel::DEBUG, tag, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define LOG_D(tag, format, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= 5
#define LOG_V(tag, format, ...) \
    log_helper(LogLevel::VERBOSE, tag, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define LOG_V(tag, format, ...) do {} while(0)
#endif
