/**
 * @file logger.c
 * @brief NetChunk logging system implementation
 *
 * Multi-level logging with file output, rotation, and performance optimization.
 */

#include "logger.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Global logger instance
 */
netchunk_logger_context_t* netchunk_global_logger = NULL;

/**
 * @brief Get default logger configuration
 */
static netchunk_logger_config_t get_default_config(void)
{
    netchunk_logger_config_t config = { 0 };

    config.level = NETCHUNK_LOG_INFO;
    strncpy(config.log_file_path, "netchunk.log", sizeof(config.log_file_path) - 1);
    config.log_to_file = true;
    config.log_to_stdout = false;
    config.max_file_size = 10 * 1024 * 1024; // 10MB
    config.max_backup_files = 5;
    config.include_timestamp = true;
    config.include_level = true;
    config.include_location = false; // Disable by default for performance

    return config;
}

/**
 * @brief Get file size
 */
static size_t get_file_size(const char* filepath)
{
    struct stat st;
    if (stat(filepath, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

/**
 * @brief Create backup filename
 */
static void create_backup_filename(const char* original, int backup_num,
    char* backup_name, size_t backup_name_size)
{
    snprintf(backup_name, backup_name_size, "%s.%d", original, backup_num);
}

netchunk_error_t netchunk_logger_init(netchunk_logger_context_t* context,
    const netchunk_logger_config_t* config)
{
    if (!context) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(context, 0, sizeof(netchunk_logger_context_t));

    // Use provided config or defaults
    if (config) {
        context->config = *config;
    } else {
        context->config = get_default_config();
    }

    // Open log file if file logging is enabled
    if (context->config.log_to_file) {
        context->log_file = fopen(context->config.log_file_path, "a");
        if (!context->log_file) {
            return NETCHUNK_ERROR_FILE_ACCESS;
        }

        // Get current file size
        context->current_file_size = get_file_size(context->config.log_file_path);
    }

    context->initialized = true;
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_logger_set_level(netchunk_logger_context_t* context,
    netchunk_log_level_t level)
{
    if (!context || !context->initialized) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    if (level < NETCHUNK_LOG_ERROR || level > NETCHUNK_LOG_DEBUG) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    context->config.level = level;
    return NETCHUNK_SUCCESS;
}

const char* netchunk_logger_level_string(netchunk_log_level_t level)
{
    switch (level) {
    case NETCHUNK_LOG_ERROR:
        return "ERROR";
    case NETCHUNK_LOG_WARN:
        return "WARN ";
    case NETCHUNK_LOG_INFO:
        return "INFO ";
    case NETCHUNK_LOG_DEBUG:
        return "DEBUG";
    default:
        return "UNKN ";
    }
}

netchunk_error_t netchunk_logger_rotate(netchunk_logger_context_t* context)
{
    if (!context || !context->initialized || !context->config.log_to_file) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Close current log file
    if (context->log_file) {
        fclose(context->log_file);
        context->log_file = NULL;
    }

    // Rotate backup files (move .1 to .2, .2 to .3, etc.)
    for (int i = context->config.max_backup_files - 1; i > 0; i--) {
        char old_backup[1024], new_backup[1024];
        create_backup_filename(context->config.log_file_path, i, old_backup, sizeof(old_backup));
        create_backup_filename(context->config.log_file_path, i + 1, new_backup, sizeof(new_backup));

        // Move file (ignore errors if file doesn't exist)
        rename(old_backup, new_backup);
    }

    // Move current log to .1 backup
    char first_backup[1024];
    create_backup_filename(context->config.log_file_path, 1, first_backup, sizeof(first_backup));
    rename(context->config.log_file_path, first_backup);

    // Create new log file
    context->log_file = fopen(context->config.log_file_path, "w");
    if (!context->log_file) {
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    context->current_file_size = 0;
    return NETCHUNK_SUCCESS;
}

void netchunk_logger_vlog(netchunk_logger_context_t* context,
    netchunk_log_level_t level,
    const char* file,
    int line,
    const char* format,
    va_list args)
{
    if (!context || !context->initialized) {
        return;
    }

    // Check if this log level should be output
    if (level > context->config.level) {
        return;
    }

    // Build log message
    char timestamp_buf[64] = "";
    char location_buf[256] = "";
    char level_buf[16] = "";
    char message_buf[2048];

    // Format timestamp
    if (context->config.include_timestamp) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        strftime(timestamp_buf, sizeof(timestamp_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    }

    // Format level
    if (context->config.include_level) {
        snprintf(level_buf, sizeof(level_buf), "[%s]", netchunk_logger_level_string(level));
    }

    // Format location
    if (context->config.include_location && file) {
        const char* filename = strrchr(file, '/');
        filename = filename ? filename + 1 : file; // Get just the filename
        snprintf(location_buf, sizeof(location_buf), " %s:%d", filename, line);
    }

    // Format message
    vsnprintf(message_buf, sizeof(message_buf), format, args);

    // Build complete log line
    char log_line[4096];
    snprintf(log_line, sizeof(log_line), "%s%s%s%s: %s\n",
        timestamp_buf[0] ? timestamp_buf : "",
        timestamp_buf[0] ? " " : "",
        level_buf,
        location_buf,
        message_buf);

    // Output to stdout if configured
    if (context->config.log_to_stdout) {
        fputs(log_line, stdout);
        fflush(stdout);
    }

    // Output to file if configured
    if (context->config.log_to_file && context->log_file) {
        size_t line_len = strlen(log_line);

        // Check if rotation is needed
        if (context->current_file_size + line_len > context->config.max_file_size) {
            netchunk_logger_rotate(context);
        }

        if (context->log_file) {
            fputs(log_line, context->log_file);
            fflush(context->log_file); // Immediate flush for reliability
            context->current_file_size += line_len;
        }
    }
}

void netchunk_logger_log(netchunk_logger_context_t* context,
    netchunk_log_level_t level,
    const char* file,
    int line,
    const char* format,
    ...)
{
    va_list args;
    va_start(args, format);
    netchunk_logger_vlog(context, level, file, line, format, args);
    va_end(args);
}

void netchunk_logger_flush(netchunk_logger_context_t* context)
{
    if (!context || !context->initialized) {
        return;
    }

    if (context->log_file) {
        fflush(context->log_file);
    }

    if (context->config.log_to_stdout) {
        fflush(stdout);
    }
}

void netchunk_logger_cleanup(netchunk_logger_context_t* context)
{
    if (!context)
        return;

    if (context->log_file) {
        fclose(context->log_file);
        context->log_file = NULL;
    }

    context->current_file_size = 0;
    context->initialized = false;
}

netchunk_error_t netchunk_logger_init_global(const netchunk_logger_config_t* config)
{
    if (netchunk_global_logger) {
        // Already initialized
        return NETCHUNK_SUCCESS;
    }

    netchunk_global_logger = malloc(sizeof(netchunk_logger_context_t));
    if (!netchunk_global_logger) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    netchunk_error_t error = netchunk_logger_init(netchunk_global_logger, config);
    if (error != NETCHUNK_SUCCESS) {
        free(netchunk_global_logger);
        netchunk_global_logger = NULL;
        return error;
    }

    return NETCHUNK_SUCCESS;
}

void netchunk_logger_cleanup_global(void)
{
    if (netchunk_global_logger) {
        netchunk_logger_cleanup(netchunk_global_logger);
        free(netchunk_global_logger);
        netchunk_global_logger = NULL;
    }
}
