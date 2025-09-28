#ifndef NETCHUNK_CONFIG_H
#define NETCHUNK_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct netchunk_config netchunk_config_t;
typedef struct netchunk_server netchunk_server_t;
typedef enum netchunk_error netchunk_error_t;
typedef enum netchunk_log_level netchunk_log_level_t;

// Maximum limits
#define NETCHUNK_MAX_SERVERS 32
#define NETCHUNK_MAX_PATH_LEN 1024
#define NETCHUNK_MAX_HOST_LEN 256
#define NETCHUNK_MAX_USER_LEN 64
#define NETCHUNK_MAX_PASS_LEN 128
#define NETCHUNK_MAX_SERVER_ID_LEN 64
#define NETCHUNK_MIN_CHUNK_SIZE (1024 * 1024) // 1MB
#define NETCHUNK_MAX_CHUNK_SIZE (64 * 1024 * 1024) // 64MB
#define NETCHUNK_DEFAULT_CHUNK_SIZE (4 * 1024 * 1024) // 4MB
#define NETCHUNK_MIN_REPLICATION_FACTOR 1
#define NETCHUNK_MAX_REPLICATION_FACTOR 10
#define NETCHUNK_DEFAULT_REPLICATION_FACTOR 3

// Error codes
typedef enum netchunk_error {
    NETCHUNK_SUCCESS = 0,
    NETCHUNK_ERROR_INVALID_ARGUMENT = -1,
    NETCHUNK_ERROR_OUT_OF_MEMORY = -2,
    NETCHUNK_ERROR_FILE_NOT_FOUND = -3,
    NETCHUNK_ERROR_FILE_ACCESS = -4,
    NETCHUNK_ERROR_NETWORK = -5,
    NETCHUNK_ERROR_FTP = -6,
    NETCHUNK_ERROR_CONFIG = -7,
    NETCHUNK_ERROR_CONFIG_PARSE = -8,
    NETCHUNK_ERROR_CONFIG_VALIDATION = -9,
    NETCHUNK_ERROR_CHUNK_INTEGRITY = -10,
    NETCHUNK_ERROR_MANIFEST_CORRUPT = -11,
    NETCHUNK_ERROR_SERVER_UNAVAILABLE = -12,
    NETCHUNK_ERROR_INSUFFICIENT_SERVERS = -13,
    NETCHUNK_ERROR_CRYPTO = -14,
    NETCHUNK_ERROR_TIMEOUT = -15,
    NETCHUNK_ERROR_CANCELLED = -16,
    NETCHUNK_ERROR_UPLOAD_FAILED = -17,
    NETCHUNK_ERROR_DOWNLOAD_FAILED = -18,
    NETCHUNK_ERROR_EOF = -19,
    NETCHUNK_ERROR_UNKNOWN = -99
} netchunk_error_t;

// Log levels
typedef enum netchunk_log_level {
    NETCHUNK_LOG_ERROR = 0,
    NETCHUNK_LOG_WARN = 1,
    NETCHUNK_LOG_INFO = 2,
    NETCHUNK_LOG_DEBUG = 3
} netchunk_log_level_t;

// Server connection status
typedef enum netchunk_server_status {
    NETCHUNK_SERVER_UNKNOWN = 0,
    NETCHUNK_SERVER_AVAILABLE = 1,
    NETCHUNK_SERVER_UNAVAILABLE = 2,
    NETCHUNK_SERVER_DEGRADED = 3
} netchunk_server_status_t;

// FTP server configuration
typedef struct netchunk_server {
    char id[NETCHUNK_MAX_SERVER_ID_LEN]; // Server identifier
    char host[NETCHUNK_MAX_HOST_LEN];
    uint16_t port;
    char username[NETCHUNK_MAX_USER_LEN];
    char password[NETCHUNK_MAX_PASS_LEN];
    char base_path[NETCHUNK_MAX_PATH_LEN];
    bool use_ssl;
    bool passive_mode;
    int priority;
    netchunk_server_status_t status;
    time_t last_health_check;
    double last_latency_ms;
    uint64_t bytes_available;
    uint64_t bytes_used;
} netchunk_server_t;

// Main configuration structure
typedef struct netchunk_config {
    // General settings
    size_t chunk_size;
    int replication_factor;
    int max_concurrent_operations;
    int ftp_timeout;
    int max_retry_attempts; // Maximum retry attempts for operations
    char local_storage_path[NETCHUNK_MAX_PATH_LEN];
    netchunk_log_level_t log_level;
    char log_file[NETCHUNK_MAX_PATH_LEN];
    bool health_monitoring_enabled;
    int health_check_interval;

    // Server configurations
    netchunk_server_t servers[NETCHUNK_MAX_SERVERS];
    int server_count;

    // Repair settings
    bool auto_repair_enabled;
    int max_repair_attempts;
    int repair_delay;
    bool rebalancing_enabled;

    // Monitoring settings
    int storage_alert_threshold;
    int latency_alert_threshold;
    bool performance_logging;
    char monitoring_data_path[NETCHUNK_MAX_PATH_LEN];

    // Security settings
    bool verify_ssl_certificates;
    bool always_verify_integrity;
    bool encrypt_chunks;
} netchunk_config_t;

// Configuration parsing functions
netchunk_error_t netchunk_config_init_defaults(netchunk_config_t* config);
netchunk_error_t netchunk_config_load(netchunk_config_t* config, const char* config_path);
netchunk_error_t netchunk_config_load_file(netchunk_config_t* config, const char* config_path);
netchunk_error_t netchunk_config_validate(const netchunk_config_t* config);
netchunk_error_t netchunk_config_find_file(char* config_path, size_t path_len);
const char* netchunk_error_string(netchunk_error_t error);
netchunk_log_level_t netchunk_log_level_from_string(const char* level_str);
const char* netchunk_log_level_to_string(netchunk_log_level_t level);
netchunk_error_t netchunk_config_expand_path(const char* path, char* expanded_path, size_t max_len);
void netchunk_config_cleanup(netchunk_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // NETCHUNK_CONFIG_H
