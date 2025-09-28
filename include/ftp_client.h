#ifndef NETCHUNK_FTP_CLIENT_H
#define NETCHUNK_FTP_CLIENT_H

#include "config.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct netchunk_ftp_pool netchunk_ftp_pool_t;
typedef struct netchunk_ftp_connection netchunk_ftp_connection_t;
typedef struct netchunk_upload_progress netchunk_upload_progress_t;
typedef struct netchunk_download_progress netchunk_download_progress_t;
typedef struct netchunk_ftp_stats netchunk_ftp_stats_t;
typedef struct netchunk_ftp_context netchunk_ftp_context_t;
typedef struct netchunk_chunk netchunk_chunk_t;
typedef struct netchunk_file_manifest netchunk_file_manifest_t;

// Connection pool constants
#define NETCHUNK_FTP_POOL_MAX_CONNECTIONS 16
#define NETCHUNK_FTP_MAX_RETRIES 3
#define NETCHUNK_FTP_RETRY_DELAY_BASE 1000 // milliseconds
#define NETCHUNK_FTP_CONNECTION_TIMEOUT 30 // seconds
#define NETCHUNK_FTP_MAX_REDIRECTS 5

// FTP connection status
typedef enum netchunk_ftp_status {
    NETCHUNK_FTP_STATUS_DISCONNECTED = 0,
    NETCHUNK_FTP_STATUS_CONNECTING = 1,
    NETCHUNK_FTP_STATUS_CONNECTED = 2,
    NETCHUNK_FTP_STATUS_BUSY = 3,
    NETCHUNK_FTP_STATUS_ERROR = 4
} netchunk_ftp_status_t;

// Progress callback function types
typedef void (*netchunk_upload_progress_callback_t)(void* userdata, double total_bytes, double uploaded_bytes);
typedef void (*netchunk_download_progress_callback_t)(void* userdata, double total_bytes, double downloaded_bytes);

// Upload progress information
typedef struct netchunk_upload_progress {
    void* userdata;
    netchunk_upload_progress_callback_t callback;
    double total_bytes;
    double uploaded_bytes;
    time_t start_time;
    double transfer_rate_bps;
    bool cancelled;
} netchunk_upload_progress_t;

// Download progress information
typedef struct netchunk_download_progress {
    void* userdata;
    netchunk_download_progress_callback_t callback;
    double total_bytes;
    double downloaded_bytes;
    time_t start_time;
    double transfer_rate_bps;
    bool cancelled;
} netchunk_download_progress_t;

// FTP connection statistics
typedef struct netchunk_ftp_stats {
    uint64_t bytes_uploaded;
    uint64_t bytes_downloaded;
    uint32_t successful_operations;
    uint32_t failed_operations;
    uint32_t retries_performed;
    time_t last_activity;
    double average_latency_ms;
    uint32_t connection_errors;
} netchunk_ftp_stats_t;

// Individual FTP connection
typedef struct netchunk_ftp_connection {
    CURL* curl_handle;
    netchunk_server_t* server;
    netchunk_ftp_status_t status;
    time_t last_used;
    time_t connected_at;
    pthread_mutex_t mutex;
    netchunk_ftp_stats_t stats;
    int retry_count;
    bool in_use;
    char error_message[256];
} netchunk_ftp_connection_t;

// FTP connection pool
typedef struct netchunk_ftp_pool {
    netchunk_ftp_connection_t connections[NETCHUNK_FTP_POOL_MAX_CONNECTIONS];
    int connection_count;
    pthread_mutex_t pool_mutex;
    pthread_cond_t connection_available;
    bool initialized;
    int max_concurrent;
    netchunk_config_t* config;
} netchunk_ftp_pool_t;

// Memory buffer for FTP operations
typedef struct netchunk_memory_buffer {
    uint8_t* data;
    size_t size;
    size_t capacity;
    size_t position; // For reading operations
} netchunk_memory_buffer_t;

// FTP context structure (main interface)
typedef struct netchunk_ftp_context {
    netchunk_ftp_pool_t* pool; // Connection pool
    netchunk_config_t* config; // Configuration reference
    bool initialized; // Initialization flag
} netchunk_ftp_context_t;

// FTP Pool Management Functions

/**
 * @brief Initialize the FTP connection pool
 * @param pool Pointer to FTP pool structure
 * @param config NetChunk configuration
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_pool_init(netchunk_ftp_pool_t* pool, netchunk_config_t* config);

/**
 * @brief Cleanup and destroy the FTP connection pool
 * @param pool Pointer to FTP pool structure
 */
void netchunk_ftp_pool_cleanup(netchunk_ftp_pool_t* pool);

/**
 * @brief Get a connection from the pool for a specific server
 * @param pool Pointer to FTP pool structure
 * @param server_id Server index in configuration
 * @param connection Output pointer for acquired connection
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_pool_acquire(netchunk_ftp_pool_t* pool, int server_id, netchunk_ftp_connection_t** connection);

/**
 * @brief Return a connection to the pool
 * @param pool Pointer to FTP pool structure
 * @param connection Connection to return
 */
void netchunk_ftp_pool_release(netchunk_ftp_pool_t* pool, netchunk_ftp_connection_t* connection);

/**
 * @brief Test connectivity to all servers in the pool
 * @param pool Pointer to FTP pool structure
 * @return NETCHUNK_SUCCESS if all servers are reachable, error code otherwise
 */
netchunk_error_t netchunk_ftp_pool_test_connectivity(netchunk_ftp_pool_t* pool);

// FTP Operation Functions

/**
 * @brief Upload data to an FTP server
 * @param connection FTP connection to use
 * @param remote_path Remote file path
 * @param data Data buffer to upload
 * @param data_size Size of data to upload
 * @param progress Progress callback structure (optional)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_upload(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    const uint8_t* data,
    size_t data_size,
    netchunk_upload_progress_t* progress);

/**
 * @brief Upload data from file to FTP server
 * @param connection FTP connection to use
 * @param remote_path Remote file path
 * @param local_file_path Local file to upload
 * @param progress Progress callback structure (optional)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_upload_file(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    const char* local_file_path,
    netchunk_upload_progress_t* progress);

/**
 * @brief Download data from an FTP server
 * @param connection FTP connection to use
 * @param remote_path Remote file path
 * @param buffer Output buffer for downloaded data
 * @param progress Progress callback structure (optional)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_download(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    netchunk_memory_buffer_t* buffer,
    netchunk_download_progress_t* progress);

/**
 * @brief Download data from FTP server to file
 * @param connection FTP connection to use
 * @param remote_path Remote file path
 * @param local_file_path Local file to save to
 * @param progress Progress callback structure (optional)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_download_file(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    const char* local_file_path,
    netchunk_download_progress_t* progress);

/**
 * @brief Delete a file on the FTP server
 * @param connection FTP connection to use
 * @param remote_path Remote file path to delete
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_delete(netchunk_ftp_connection_t* connection, const char* remote_path);

/**
 * @brief Check if a file exists on the FTP server
 * @param connection FTP connection to use
 * @param remote_path Remote file path to check
 * @param exists Output boolean indicating if file exists
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_file_exists(netchunk_ftp_connection_t* connection, const char* remote_path, bool* exists);

/**
 * @brief Get file size on the FTP server
 * @param connection FTP connection to use
 * @param remote_path Remote file path
 * @param size Output file size
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_get_file_size(netchunk_ftp_connection_t* connection, const char* remote_path, size_t* size);

/**
 * @brief Create directory on FTP server
 * @param connection FTP connection to use
 * @param remote_path Remote directory path to create
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_mkdir(netchunk_ftp_connection_t* connection, const char* remote_path);

/**
 * @brief List directory contents on FTP server
 * @param connection FTP connection to use
 * @param remote_path Remote directory path
 * @param buffer Output buffer for directory listing
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_list_directory(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    netchunk_memory_buffer_t* buffer);

// Memory Buffer Functions

/**
 * @brief Initialize a memory buffer
 * @param buffer Pointer to memory buffer structure
 * @param initial_capacity Initial capacity in bytes
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_memory_buffer_init(netchunk_memory_buffer_t* buffer, size_t initial_capacity);

/**
 * @brief Cleanup and free memory buffer
 * @param buffer Pointer to memory buffer structure
 */
void netchunk_memory_buffer_cleanup(netchunk_memory_buffer_t* buffer);

/**
 * @brief Resize memory buffer to new capacity
 * @param buffer Pointer to memory buffer structure
 * @param new_capacity New capacity in bytes
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_memory_buffer_resize(netchunk_memory_buffer_t* buffer, size_t new_capacity);

/**
 * @brief Append data to memory buffer
 * @param buffer Pointer to memory buffer structure
 * @param data Data to append
 * @param data_size Size of data to append
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_memory_buffer_append(netchunk_memory_buffer_t* buffer, const uint8_t* data, size_t data_size);

// Utility Functions

/**
 * @brief Test connection to a single server
 * @param server Server configuration
 * @param latency_ms Output latency measurement
 * @return NETCHUNK_SUCCESS if server is reachable, error code otherwise
 */
netchunk_error_t netchunk_ftp_test_server(const netchunk_server_t* server, double* latency_ms);

/**
 * @brief Build complete FTP URL from server config and path
 * @param server Server configuration
 * @param remote_path Remote file/directory path
 * @param url_buffer Output buffer for URL
 * @param buffer_size Size of URL buffer
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_build_url(const netchunk_server_t* server,
    const char* remote_path,
    char* url_buffer,
    size_t buffer_size);

/**
 * @brief Get last error message from connection
 * @param connection FTP connection
 * @return Error message string
 */
const char* netchunk_ftp_get_error_message(const netchunk_ftp_connection_t* connection);

/**
 * @brief Reset connection statistics
 * @param connection FTP connection
 */
void netchunk_ftp_reset_stats(netchunk_ftp_connection_t* connection);

// High-level FTP Context Functions

/**
 * @brief Initialize FTP context
 * @param context FTP context to initialize
 * @param config NetChunk configuration
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_init(netchunk_ftp_context_t* context, netchunk_config_t* config);

/**
 * @brief Cleanup FTP context
 * @param context FTP context to cleanup
 */
void netchunk_ftp_cleanup(netchunk_ftp_context_t* context);

/**
 * @brief Test connection to a server
 * @param context FTP context
 * @param server Server to test
 * @return NETCHUNK_SUCCESS if connection successful, error code otherwise
 */
netchunk_error_t netchunk_ftp_test_connection(netchunk_ftp_context_t* context,
    const netchunk_server_t* server);

// Chunk-specific Functions

/**
 * @brief Upload chunk to FTP server
 * @param context FTP context
 * @param server Server to upload to
 * @param chunk Chunk to upload
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_upload_chunk(netchunk_ftp_context_t* context,
    const netchunk_server_t* server,
    const netchunk_chunk_t* chunk);

/**
 * @brief Download chunk from FTP server
 * @param context FTP context
 * @param server Server to download from
 * @param chunk Chunk to download (data will be allocated)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_download_chunk(netchunk_ftp_context_t* context,
    const netchunk_server_t* server,
    netchunk_chunk_t* chunk);

/**
 * @brief Delete chunk from FTP server
 * @param context FTP context
 * @param server Server to delete from
 * @param chunk Chunk to delete
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_delete_chunk(netchunk_ftp_context_t* context,
    const netchunk_server_t* server,
    const netchunk_chunk_t* chunk);

// Manifest-specific Functions

/**
 * @brief Upload manifest to FTP servers
 * @param context FTP context
 * @param config NetChunk configuration
 * @param manifest Manifest to upload
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_upload_manifest(netchunk_ftp_context_t* context,
    netchunk_config_t* config,
    const netchunk_file_manifest_t* manifest);

/**
 * @brief Download manifest from FTP servers
 * @param context FTP context
 * @param config NetChunk configuration
 * @param remote_name Remote file name
 * @param manifest Output manifest structure
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_download_manifest(netchunk_ftp_context_t* context,
    netchunk_config_t* config,
    const char* remote_name,
    netchunk_file_manifest_t* manifest);

/**
 * @brief Delete manifest from FTP servers
 * @param context FTP context
 * @param config NetChunk configuration
 * @param remote_name Remote file name
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_delete_manifest(netchunk_ftp_context_t* context,
    netchunk_config_t* config,
    const char* remote_name);

/**
 * @brief List all manifests on FTP servers
 * @param context FTP context
 * @param config NetChunk configuration
 * @param files Output array of manifests (allocated by function)
 * @param count Output count of files
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_ftp_list_manifests(netchunk_ftp_context_t* context,
    netchunk_config_t* config,
    netchunk_file_manifest_t** files,
    size_t* count);

#ifdef __cplusplus
}
#endif

#endif // NETCHUNK_FTP_CLIENT_H
