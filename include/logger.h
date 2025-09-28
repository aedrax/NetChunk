/**
 * @file logger.h
 * @brief NetChunk logging system
 *
 * Multi-level logging with file output, rotation, and performance optimization.
 * Provides comprehensive audit trails and debugging capabilities.
 */

#ifndef NETCHUNK_LOGGER_H
#define NETCHUNK_LOGGER_H

#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log levels are defined in config.h

/**
 * @brief Logger configuration
 */
typedef struct {
    netchunk_log_level_t level; // Current log level
    char log_file_path[512]; // Log file path
    bool log_to_file; // Enable file logging
    bool log_to_stdout; // Enable stdout logging
    size_t max_file_size; // Max log file size (bytes)
    int max_backup_files; // Max backup files to keep
    bool include_timestamp; // Include timestamp in logs
    bool include_level; // Include level in logs
    bool include_location; // Include file:line in logs
} netchunk_logger_config_t;

/**
 * @brief Logger context
 */
typedef struct {
    netchunk_logger_config_t config; // Logger configuration
    FILE* log_file; // Current log file handle
    size_t current_file_size; // Current log file size
    bool initialized; // Initialization flag
} netchunk_logger_context_t;

/**
 * @brief Initialize logger with configuration
 *
 * @param context Logger context to initialize
 * @param config Logger configuration (NULL for defaults)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_logger_init(netchunk_logger_context_t* context,
    const netchunk_logger_config_t* config);

/**
 * @brief Set log level
 *
 * @param context Logger context
 * @param level New log level
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_logger_set_level(netchunk_logger_context_t* context,
    netchunk_log_level_t level);

/**
 * @brief Log a message with specified level
 *
 * @param context Logger context
 * @param level Log level
 * @param file Source file name (use __FILE__)
 * @param line Source line number (use __LINE__)
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void netchunk_logger_log(netchunk_logger_context_t* context,
    netchunk_log_level_t level,
    const char* file,
    int line,
    const char* format,
    ...);

/**
 * @brief Log a message with vprintf-style arguments
 *
 * @param context Logger context
 * @param level Log level
 * @param file Source file name
 * @param line Source line number
 * @param format Printf-style format string
 * @param args Variable argument list
 */
void netchunk_logger_vlog(netchunk_logger_context_t* context,
    netchunk_log_level_t level,
    const char* file,
    int line,
    const char* format,
    va_list args);

/**
 * @brief Force log rotation
 *
 * @param context Logger context
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_logger_rotate(netchunk_logger_context_t* context);

/**
 * @brief Flush all pending log messages
 *
 * @param context Logger context
 */
void netchunk_logger_flush(netchunk_logger_context_t* context);

/**
 * @brief Get string representation of log level
 *
 * @param level Log level
 * @return String representation
 */
const char* netchunk_logger_level_string(netchunk_log_level_t level);

/**
 * @brief Cleanup logger and close files
 *
 * @param context Logger context to cleanup
 */
void netchunk_logger_cleanup(netchunk_logger_context_t* context);

/**
 * @brief Convenience macros for logging
 */
#define NETCHUNK_LOG_ERROR_MSG(ctx, fmt, ...) \
    netchunk_logger_log(ctx, NETCHUNK_LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NETCHUNK_LOG_WARN_MSG(ctx, fmt, ...) \
    netchunk_logger_log(ctx, NETCHUNK_LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NETCHUNK_LOG_INFO_MSG(ctx, fmt, ...) \
    netchunk_logger_log(ctx, NETCHUNK_LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NETCHUNK_LOG_DEBUG_MSG(ctx, fmt, ...) \
    netchunk_logger_log(ctx, NETCHUNK_LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/**
 * @brief Global logger instance (if desired)
 *
 * Note: This is optional - applications can manage their own logger contexts
 */
extern netchunk_logger_context_t* netchunk_global_logger;

/**
 * @brief Initialize global logger
 *
 * @param config Logger configuration (NULL for defaults)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_logger_init_global(const netchunk_logger_config_t* config);

/**
 * @brief Cleanup global logger
 */
void netchunk_logger_cleanup_global(void);

/**
 * @brief Global logging macros (use global logger instance)
 */
#define NETCHUNK_ERROR(fmt, ...)                \
    if (netchunk_global_logger)                 \
    netchunk_logger_log(netchunk_global_logger, \
        NETCHUNK_LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NETCHUNK_WARN(fmt, ...)                 \
    if (netchunk_global_logger)                 \
    netchunk_logger_log(netchunk_global_logger, \
        NETCHUNK_LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NETCHUNK_INFO(fmt, ...)                 \
    if (netchunk_global_logger)                 \
    netchunk_logger_log(netchunk_global_logger, \
        NETCHUNK_LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NETCHUNK_DEBUG(fmt, ...)                \
    if (netchunk_global_logger)                 \
    netchunk_logger_log(netchunk_global_logger, \
        NETCHUNK_LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* NETCHUNK_LOGGER_H */
