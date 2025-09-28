#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Internal helper functions
static netchunk_error_t parse_ini_file(netchunk_config_t* config, FILE* file);
static netchunk_error_t parse_section(netchunk_config_t* config, const char* section, const char* key, const char* value);
static void trim_whitespace(char* str);
static bool parse_bool(const char* value);
static long parse_int(const char* value);
static size_t parse_size(const char* value);

netchunk_error_t netchunk_config_init_defaults(netchunk_config_t* config)
{
    if (!config) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(config, 0, sizeof(netchunk_config_t));

    // General settings defaults
    config->chunk_size = NETCHUNK_DEFAULT_CHUNK_SIZE;
    config->replication_factor = NETCHUNK_DEFAULT_REPLICATION_FACTOR;
    config->max_concurrent_operations = 4;
    config->ftp_timeout = 30;
    strcpy(config->local_storage_path, "~/.netchunk/data");
    config->log_level = NETCHUNK_LOG_INFO;
    strcpy(config->log_file, "~/.netchunk/netchunk.log");
    config->health_monitoring_enabled = true;
    config->health_check_interval = 300;

    // No servers configured by default
    config->server_count = 0;

    // Repair settings defaults
    config->auto_repair_enabled = true;
    config->max_repair_attempts = 3;
    config->repair_delay = 10;
    config->rebalancing_enabled = true;

    // Monitoring settings defaults
    config->storage_alert_threshold = 85;
    config->latency_alert_threshold = 1000;
    config->performance_logging = false;
    strcpy(config->monitoring_data_path, "~/.netchunk/monitoring");

    // Security settings defaults
    config->verify_ssl_certificates = true;
    config->always_verify_integrity = true;
    config->encrypt_chunks = false;

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_config_load(netchunk_config_t* config, const char* config_path)
{
    return netchunk_config_load_file(config, config_path);
}

netchunk_error_t netchunk_config_load_file(netchunk_config_t* config, const char* config_path)
{
    if (!config || !config_path) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Initialize with defaults first
    netchunk_error_t error = netchunk_config_init_defaults(config);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    // Expand path if it contains ~
    char expanded_path[NETCHUNK_MAX_PATH_LEN];
    error = netchunk_config_expand_path(config_path, expanded_path, sizeof(expanded_path));
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    FILE* file = fopen(expanded_path, "r");
    if (!file) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    }

    error = parse_ini_file(config, file);
    fclose(file);

    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    return netchunk_config_validate(config);
}

netchunk_error_t netchunk_config_validate(const netchunk_config_t* config)
{
    if (!config) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Validate chunk size
    if (config->chunk_size < NETCHUNK_MIN_CHUNK_SIZE || config->chunk_size > NETCHUNK_MAX_CHUNK_SIZE) {
        return NETCHUNK_ERROR_CONFIG_VALIDATION;
    }

    // Validate replication factor
    if (config->replication_factor < NETCHUNK_MIN_REPLICATION_FACTOR || config->replication_factor > NETCHUNK_MAX_REPLICATION_FACTOR) {
        return NETCHUNK_ERROR_CONFIG_VALIDATION;
    }

    // Must have at least one server
    if (config->server_count < 1) {
        return NETCHUNK_ERROR_CONFIG_VALIDATION;
    }

    // Must have at least as many servers as replication factor
    if (config->server_count < config->replication_factor) {
        return NETCHUNK_ERROR_INSUFFICIENT_SERVERS;
    }

    // Validate each server configuration
    for (int i = 0; i < config->server_count; i++) {
        const netchunk_server_t* server = &config->servers[i];

        if (strlen(server->host) == 0) {
            return NETCHUNK_ERROR_CONFIG_VALIDATION;
        }

        if (server->port == 0 || server->port > 65535) {
            return NETCHUNK_ERROR_CONFIG_VALIDATION;
        }

        if (strlen(server->username) == 0) {
            return NETCHUNK_ERROR_CONFIG_VALIDATION;
        }

        if (strlen(server->base_path) == 0) {
            return NETCHUNK_ERROR_CONFIG_VALIDATION;
        }
    }

    // Validate other settings
    if (config->max_concurrent_operations < 1 || config->max_concurrent_operations > 32) {
        return NETCHUNK_ERROR_CONFIG_VALIDATION;
    }

    if (config->ftp_timeout < 5 || config->ftp_timeout > 300) {
        return NETCHUNK_ERROR_CONFIG_VALIDATION;
    }

    if (config->health_check_interval < 30 || config->health_check_interval > 3600) {
        return NETCHUNK_ERROR_CONFIG_VALIDATION;
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_config_find_file(char* config_path, size_t path_len)
{
    if (!config_path || path_len == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    const char* search_paths[] = {
        "netchunk.conf",
        "~/.netchunk/netchunk.conf",
        "~/.netchunk/config",
        "/etc/netchunk/netchunk.conf",
        "/usr/local/etc/netchunk/netchunk.conf"
    };

    const size_t num_paths = sizeof(search_paths) / sizeof(search_paths[0]);

    for (size_t i = 0; i < num_paths; i++) {
        char expanded_path[NETCHUNK_MAX_PATH_LEN];
        netchunk_error_t error = netchunk_config_expand_path(search_paths[i], expanded_path, sizeof(expanded_path));
        if (error != NETCHUNK_SUCCESS) {
            continue;
        }

        if (access(expanded_path, R_OK) == 0) {
            if (strlen(expanded_path) >= path_len) {
                return NETCHUNK_ERROR_INVALID_ARGUMENT;
            }
            strcpy(config_path, expanded_path);
            return NETCHUNK_SUCCESS;
        }
    }

    return NETCHUNK_ERROR_FILE_NOT_FOUND;
}

const char* netchunk_error_string(netchunk_error_t error)
{
    switch (error) {
    case NETCHUNK_SUCCESS:
        return "Success";
    case NETCHUNK_ERROR_INVALID_ARGUMENT:
        return "Invalid argument";
    case NETCHUNK_ERROR_OUT_OF_MEMORY:
        return "Out of memory";
    case NETCHUNK_ERROR_FILE_NOT_FOUND:
        return "File not found";
    case NETCHUNK_ERROR_FILE_ACCESS:
        return "File access error";
    case NETCHUNK_ERROR_NETWORK:
        return "Network error";
    case NETCHUNK_ERROR_FTP:
        return "FTP error";
    case NETCHUNK_ERROR_CONFIG_PARSE:
        return "Configuration parse error";
    case NETCHUNK_ERROR_CONFIG_VALIDATION:
        return "Configuration validation error";
    case NETCHUNK_ERROR_CHUNK_INTEGRITY:
        return "Chunk integrity error";
    case NETCHUNK_ERROR_MANIFEST_CORRUPT:
        return "Manifest corruption error";
    case NETCHUNK_ERROR_SERVER_UNAVAILABLE:
        return "Server unavailable";
    case NETCHUNK_ERROR_INSUFFICIENT_SERVERS:
        return "Insufficient servers";
    case NETCHUNK_ERROR_CRYPTO:
        return "Cryptographic error";
    case NETCHUNK_ERROR_TIMEOUT:
        return "Operation timeout";
    case NETCHUNK_ERROR_CANCELLED:
        return "Operation cancelled";
    default:
        return "Unknown error";
    }
}

netchunk_log_level_t netchunk_log_level_from_string(const char* level_str)
{
    if (!level_str) {
        return NETCHUNK_LOG_INFO;
    }

    if (strcasecmp(level_str, "ERROR") == 0) {
        return NETCHUNK_LOG_ERROR;
    } else if (strcasecmp(level_str, "WARN") == 0 || strcasecmp(level_str, "WARNING") == 0) {
        return NETCHUNK_LOG_WARN;
    } else if (strcasecmp(level_str, "INFO") == 0) {
        return NETCHUNK_LOG_INFO;
    } else if (strcasecmp(level_str, "DEBUG") == 0) {
        return NETCHUNK_LOG_DEBUG;
    }

    return NETCHUNK_LOG_INFO;
}

const char* netchunk_log_level_to_string(netchunk_log_level_t level)
{
    switch (level) {
    case NETCHUNK_LOG_ERROR:
        return "ERROR";
    case NETCHUNK_LOG_WARN:
        return "WARN";
    case NETCHUNK_LOG_INFO:
        return "INFO";
    case NETCHUNK_LOG_DEBUG:
        return "DEBUG";
    default:
        return "INFO";
    }
}

netchunk_error_t netchunk_config_expand_path(const char* path, char* expanded_path, size_t max_len)
{
    if (!path || !expanded_path || max_len == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    if (path[0] != '~') {
        // No expansion needed
        if (strlen(path) >= max_len) {
            return NETCHUNK_ERROR_INVALID_ARGUMENT;
        }
        strcpy(expanded_path, path);
        return NETCHUNK_SUCCESS;
    }

    // Get home directory
    const char* home_dir = getenv("HOME");
    if (!home_dir) {
        struct passwd* pw = getpwuid(getuid());
        if (!pw) {
            return NETCHUNK_ERROR_FILE_ACCESS;
        }
        home_dir = pw->pw_dir;
    }

    // Expand path
    size_t home_len = strlen(home_dir);
    size_t path_len = strlen(path);

    if (path_len == 1) {
        // Just "~"
        if (home_len >= max_len) {
            return NETCHUNK_ERROR_INVALID_ARGUMENT;
        }
        strcpy(expanded_path, home_dir);
    } else {
        // "~/something"
        if (home_len + path_len - 1 >= max_len) {
            return NETCHUNK_ERROR_INVALID_ARGUMENT;
        }
        strcpy(expanded_path, home_dir);
        strcat(expanded_path, path + 1); // Skip the '~'
    }

    return NETCHUNK_SUCCESS;
}

void netchunk_config_cleanup(netchunk_config_t* config)
{
    // Currently nothing to free, but this function exists for future
    // expansion if we add dynamically allocated members
    (void)config;
}

// Internal helper functions

static netchunk_error_t parse_ini_file(netchunk_config_t* config, FILE* file)
{
    char line[1024];
    char current_section[64] = "";
    int line_number = 0;

    while (fgets(line, sizeof(line), file)) {
        line_number++;
        trim_whitespace(line);

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Check for section header
        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (!end) {
                return NETCHUNK_ERROR_CONFIG_PARSE;
            }
            *end = '\0';
            strcpy(current_section, line + 1);
            continue;
        }

        // Parse key=value pairs
        char* equals = strchr(line, '=');
        if (!equals) {
            return NETCHUNK_ERROR_CONFIG_PARSE;
        }

        *equals = '\0';
        char* key = line;
        char* value = equals + 1;

        trim_whitespace(key);
        trim_whitespace(value);

        netchunk_error_t error = parse_section(config, current_section, key, value);
        if (error != NETCHUNK_SUCCESS) {
            return error;
        }
    }

    return NETCHUNK_SUCCESS;
}

static netchunk_error_t parse_section(netchunk_config_t* config, const char* section, const char* key, const char* value)
{
    if (strcmp(section, "general") == 0) {
        if (strcmp(key, "chunk_size") == 0) {
            config->chunk_size = parse_size(value);
        } else if (strcmp(key, "replication_factor") == 0) {
            config->replication_factor = (int)parse_int(value);
        } else if (strcmp(key, "max_concurrent_operations") == 0) {
            config->max_concurrent_operations = (int)parse_int(value);
        } else if (strcmp(key, "ftp_timeout") == 0) {
            config->ftp_timeout = (int)parse_int(value);
        } else if (strcmp(key, "local_storage_path") == 0) {
            strncpy(config->local_storage_path, value, NETCHUNK_MAX_PATH_LEN - 1);
        } else if (strcmp(key, "log_level") == 0) {
            config->log_level = netchunk_log_level_from_string(value);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy(config->log_file, value, NETCHUNK_MAX_PATH_LEN - 1);
        } else if (strcmp(key, "health_monitoring_enabled") == 0) {
            config->health_monitoring_enabled = parse_bool(value);
        } else if (strcmp(key, "health_check_interval") == 0) {
            config->health_check_interval = (int)parse_int(value);
        }
    } else if (strncmp(section, "server_", 7) == 0) {
        // Parse server number
        int server_num = (int)parse_int(section + 7) - 1; // Convert to 0-based index
        if (server_num < 0 || server_num >= NETCHUNK_MAX_SERVERS) {
            return NETCHUNK_ERROR_CONFIG_PARSE;
        }

        // Ensure we have enough servers allocated
        if (server_num >= config->server_count) {
            config->server_count = server_num + 1;
        }

        netchunk_server_t* server = &config->servers[server_num];

        if (strcmp(key, "host") == 0) {
            strncpy(server->host, value, NETCHUNK_MAX_HOST_LEN - 1);
        } else if (strcmp(key, "port") == 0) {
            server->port = (uint16_t)parse_int(value);
        } else if (strcmp(key, "username") == 0) {
            strncpy(server->username, value, NETCHUNK_MAX_USER_LEN - 1);
        } else if (strcmp(key, "password") == 0) {
            strncpy(server->password, value, NETCHUNK_MAX_PASS_LEN - 1);
        } else if (strcmp(key, "base_path") == 0) {
            strncpy(server->base_path, value, NETCHUNK_MAX_PATH_LEN - 1);
        } else if (strcmp(key, "use_ssl") == 0) {
            server->use_ssl = parse_bool(value);
        } else if (strcmp(key, "passive_mode") == 0) {
            server->passive_mode = parse_bool(value);
        } else if (strcmp(key, "priority") == 0) {
            server->priority = (int)parse_int(value);
        }
    } else if (strcmp(section, "repair") == 0) {
        if (strcmp(key, "auto_repair_enabled") == 0) {
            config->auto_repair_enabled = parse_bool(value);
        } else if (strcmp(key, "max_repair_attempts") == 0) {
            config->max_repair_attempts = (int)parse_int(value);
        } else if (strcmp(key, "repair_delay") == 0) {
            config->repair_delay = (int)parse_int(value);
        } else if (strcmp(key, "rebalancing_enabled") == 0) {
            config->rebalancing_enabled = parse_bool(value);
        }
    } else if (strcmp(section, "monitoring") == 0) {
        if (strcmp(key, "storage_alert_threshold") == 0) {
            config->storage_alert_threshold = (int)parse_int(value);
        } else if (strcmp(key, "latency_alert_threshold") == 0) {
            config->latency_alert_threshold = (int)parse_int(value);
        } else if (strcmp(key, "performance_logging") == 0) {
            config->performance_logging = parse_bool(value);
        } else if (strcmp(key, "monitoring_data_path") == 0) {
            strncpy(config->monitoring_data_path, value, NETCHUNK_MAX_PATH_LEN - 1);
        }
    } else if (strcmp(section, "security") == 0) {
        if (strcmp(key, "verify_ssl_certificates") == 0) {
            config->verify_ssl_certificates = parse_bool(value);
        } else if (strcmp(key, "always_verify_integrity") == 0) {
            config->always_verify_integrity = parse_bool(value);
        } else if (strcmp(key, "encrypt_chunks") == 0) {
            config->encrypt_chunks = parse_bool(value);
        }
    }
    // Ignore unknown sections/keys for forward compatibility

    return NETCHUNK_SUCCESS;
}

static void trim_whitespace(char* str)
{
    if (!str)
        return;

    // Trim leading whitespace
    char* start = str;
    while (*start && isspace(*start)) {
        start++;
    }

    // Trim trailing whitespace
    char* end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) {
        end--;
    }
    *(end + 1) = '\0';

    // Move string to beginning if we trimmed leading whitespace
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

static bool parse_bool(const char* value)
{
    if (!value)
        return false;

    return (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "1") == 0 || strcasecmp(value, "on") == 0);
}

static long parse_int(const char* value)
{
    if (!value)
        return 0;

    char* endptr;
    long result = strtol(value, &endptr, 10);

    if (*endptr != '\0') {
        return 0; // Invalid number
    }

    return result;
}

static size_t parse_size(const char* value)
{
    if (!value)
        return 0;

    char* endptr;
    unsigned long result = strtoul(value, &endptr, 10);

    // Handle size suffixes
    if (*endptr != '\0') {
        if (strcasecmp(endptr, "KB") == 0 || strcasecmp(endptr, "K") == 0) {
            result *= 1024;
        } else if (strcasecmp(endptr, "MB") == 0 || strcasecmp(endptr, "M") == 0) {
            result *= 1024 * 1024;
        } else if (strcasecmp(endptr, "GB") == 0 || strcasecmp(endptr, "G") == 0) {
            result *= 1024 * 1024 * 1024;
        } else {
            return 0; // Invalid suffix
        }
    }

    return (size_t)result;
}
