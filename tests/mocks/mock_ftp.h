#ifndef NETCHUNK_MOCK_FTP_H
#define NETCHUNK_MOCK_FTP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Maximum limits for mock FTP server
#define MOCK_FTP_MAX_SERVERS 20
#define MOCK_FTP_MAX_FILES_PER_SERVER 100
#define MOCK_FTP_MAX_HOST_LEN 256
#define MOCK_FTP_MAX_PATH_LEN 512
#define MOCK_FTP_MAX_USERNAME_LEN 64
#define MOCK_FTP_MAX_PASSWORD_LEN 64

/**
 * Mock file entry stored on mock FTP server
 */
typedef struct mock_ftp_file {
    char filename[MOCK_FTP_MAX_PATH_LEN];
    uint8_t* data;
    size_t size;
    time_t created_time;
    time_t modified_time;
    bool is_corrupted;
    double corruption_probability;
} mock_ftp_file_t;

/**
 * Mock FTP server instance
 */
typedef struct mock_ftp_server {
    char host[MOCK_FTP_MAX_HOST_LEN];
    uint16_t port;
    char username[MOCK_FTP_MAX_USERNAME_LEN];
    char password[MOCK_FTP_MAX_PASSWORD_LEN];
    
    // Server state
    bool is_available;
    bool simulate_connection_failure;
    bool simulate_upload_failure;
    bool simulate_download_failure;
    bool simulate_slow_connection;
    
    // Failure simulation parameters
    double connection_failure_rate;
    double upload_failure_rate;
    double download_failure_rate;
    uint32_t latency_ms_min;
    uint32_t latency_ms_max;
    
    // Storage simulation
    size_t storage_capacity;
    size_t storage_used;
    mock_ftp_file_t files[MOCK_FTP_MAX_FILES_PER_SERVER];
    size_t file_count;
    
    // Statistics
    uint64_t total_uploads;
    uint64_t total_downloads;
    uint64_t failed_uploads;
    uint64_t failed_downloads;
    uint64_t bytes_uploaded;
    uint64_t bytes_downloaded;
    
    // Threading/concurrency simulation
    bool simulate_concurrent_access;
    uint32_t max_concurrent_connections;
    uint32_t current_connections;
} mock_ftp_server_t;

/**
 * Mock FTP client context (replaces actual FTP client for testing)
 */
typedef struct mock_ftp_client_context {
    mock_ftp_server_t* connected_server;
    bool is_connected;
    char last_error[256];
    uint32_t timeout_ms;
    uint32_t retry_count;
    bool passive_mode;
} mock_ftp_client_context_t;

/**
 * Mock FTP operation result
 */
typedef enum mock_ftp_result {
    MOCK_FTP_SUCCESS = 0,
    MOCK_FTP_ERROR_CONNECTION_FAILED = -1,
    MOCK_FTP_ERROR_AUTH_FAILED = -2,
    MOCK_FTP_ERROR_FILE_NOT_FOUND = -3,
    MOCK_FTP_ERROR_UPLOAD_FAILED = -4,
    MOCK_FTP_ERROR_DOWNLOAD_FAILED = -5,
    MOCK_FTP_ERROR_STORAGE_FULL = -6,
    MOCK_FTP_ERROR_TIMEOUT = -7,
    MOCK_FTP_ERROR_NETWORK = -8,
    MOCK_FTP_ERROR_SERVER_UNAVAILABLE = -9,
    MOCK_FTP_ERROR_CORRUPTED_DATA = -10
} mock_ftp_result_t;

// Global mock FTP server registry
extern mock_ftp_server_t g_mock_ftp_servers[MOCK_FTP_MAX_SERVERS];
extern size_t g_mock_ftp_server_count;

// Mock FTP server management
int mock_ftp_init(void);
void mock_ftp_cleanup(void);
void mock_ftp_reset_all_servers(void);

// Mock FTP server creation and configuration
mock_ftp_server_t* mock_ftp_create_server(const char* host, uint16_t port, 
                                         const char* username, const char* password);
int mock_ftp_remove_server(const char* host, uint16_t port);
mock_ftp_server_t* mock_ftp_find_server(const char* host, uint16_t port);

// Mock FTP server configuration
void mock_ftp_set_server_availability(mock_ftp_server_t* server, bool available);
void mock_ftp_set_failure_rates(mock_ftp_server_t* server, 
                               double connection_rate, double upload_rate, double download_rate);
void mock_ftp_set_latency(mock_ftp_server_t* server, uint32_t min_ms, uint32_t max_ms);
void mock_ftp_set_storage_capacity(mock_ftp_server_t* server, size_t capacity);
void mock_ftp_simulate_corruption(mock_ftp_server_t* server, const char* filename, bool corrupted);

// Mock FTP client operations (to replace real FTP client calls in tests)
mock_ftp_client_context_t* mock_ftp_client_init(void);
void mock_ftp_client_cleanup(mock_ftp_client_context_t* ctx);

int mock_ftp_connect(mock_ftp_client_context_t* ctx, const char* host, uint16_t port,
                    const char* username, const char* password);
int mock_ftp_disconnect(mock_ftp_client_context_t* ctx);

int mock_ftp_upload_file(mock_ftp_client_context_t* ctx, const char* local_path,
                        const char* remote_path);
int mock_ftp_upload_data(mock_ftp_client_context_t* ctx, const uint8_t* data, size_t size,
                        const char* remote_path);

int mock_ftp_download_file(mock_ftp_client_context_t* ctx, const char* remote_path,
                          const char* local_path);
int mock_ftp_download_data(mock_ftp_client_context_t* ctx, const char* remote_path,
                          uint8_t** data, size_t* size);

int mock_ftp_delete_file(mock_ftp_client_context_t* ctx, const char* remote_path);
int mock_ftp_list_files(mock_ftp_client_context_t* ctx, char*** filenames, size_t* count);
bool mock_ftp_file_exists(mock_ftp_client_context_t* ctx, const char* remote_path);
size_t mock_ftp_get_file_size(mock_ftp_client_context_t* ctx, const char* remote_path);

// Mock FTP server utilities
void mock_ftp_server_clear_files(mock_ftp_server_t* server);
void mock_ftp_server_print_stats(const mock_ftp_server_t* server);
size_t mock_ftp_server_get_file_count(const mock_ftp_server_t* server);
size_t mock_ftp_server_get_storage_used(const mock_ftp_server_t* server);

// Test scenario helpers
int mock_ftp_setup_test_scenario_basic(void);
int mock_ftp_setup_test_scenario_multi_server(void);
int mock_ftp_setup_test_scenario_failures(void);
int mock_ftp_setup_test_scenario_performance(void);

// Mock FTP error handling
const char* mock_ftp_get_error_string(mock_ftp_result_t result);
const char* mock_ftp_client_get_last_error(const mock_ftp_client_context_t* ctx);

// Utility functions for testing integration
void mock_ftp_enable_detailed_logging(bool enable);
void mock_ftp_set_global_latency(uint32_t min_ms, uint32_t max_ms);
void mock_ftp_simulate_network_partition(const char* host1, const char* host2, bool partition);

#endif // NETCHUNK_MOCK_FTP_H
