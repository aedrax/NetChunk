#include "mock_ftp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

// Global mock FTP server registry
mock_ftp_server_t g_mock_ftp_servers[MOCK_FTP_MAX_SERVERS];
size_t g_mock_ftp_server_count = 0;

// Global settings
static bool g_detailed_logging = false;
static uint32_t g_global_latency_min = 0;
static uint32_t g_global_latency_max = 0;

// Random number generation for failure simulation
static uint64_t g_mock_random_state = 12345;

static uint32_t mock_ftp_random_uint32(void) {
    g_mock_random_state = g_mock_random_state * 1664525ULL + 1013904223ULL;
    return (uint32_t)(g_mock_random_state >> 16);
}

static double mock_ftp_random_double(void) {
    return (double)mock_ftp_random_uint32() / (double)UINT32_MAX;
}

static void mock_ftp_simulate_delay(uint32_t min_ms, uint32_t max_ms) {
    if (min_ms == 0 && max_ms == 0) return;
    
    uint32_t delay_ms = min_ms;
    if (max_ms > min_ms) {
        delay_ms += (mock_ftp_random_uint32() % (max_ms - min_ms));
    }
    
    if (delay_ms > 0) {
        usleep(delay_ms * 1000);
    }
}

/**
 * Mock FTP system initialization
 */
int mock_ftp_init(void) {
    // Initialize random seed
    struct timeval tv;
    gettimeofday(&tv, NULL);
    g_mock_random_state = tv.tv_sec ^ tv.tv_usec;
    
    // Clear all servers
    mock_ftp_reset_all_servers();
    
    if (g_detailed_logging) {
        printf("Mock FTP system initialized\n");
    }
    
    return 0;
}

/**
 * Mock FTP system cleanup
 */
void mock_ftp_cleanup(void) {
    mock_ftp_reset_all_servers();
    
    if (g_detailed_logging) {
        printf("Mock FTP system cleaned up\n");
    }
}

/**
 * Reset all mock FTP servers
 */
void mock_ftp_reset_all_servers(void) {
    for (size_t i = 0; i < g_mock_ftp_server_count; i++) {
        mock_ftp_server_clear_files(&g_mock_ftp_servers[i]);
    }
    
    memset(g_mock_ftp_servers, 0, sizeof(g_mock_ftp_servers));
    g_mock_ftp_server_count = 0;
}

/**
 * Create a mock FTP server
 */
mock_ftp_server_t* mock_ftp_create_server(const char* host, uint16_t port, 
                                         const char* username, const char* password) {
    if (!host || !username || !password) return NULL;
    if (g_mock_ftp_server_count >= MOCK_FTP_MAX_SERVERS) return NULL;
    
    // Check if server already exists
    mock_ftp_server_t* existing = mock_ftp_find_server(host, port);
    if (existing) return existing;
    
    // Create new server
    mock_ftp_server_t* server = &g_mock_ftp_servers[g_mock_ftp_server_count];
    memset(server, 0, sizeof(mock_ftp_server_t));
    
    strncpy(server->host, host, sizeof(server->host) - 1);
    server->port = port;
    strncpy(server->username, username, sizeof(server->username) - 1);
    strncpy(server->password, password, sizeof(server->password) - 1);
    
    // Default server settings
    server->is_available = true;
    server->storage_capacity = 1024 * 1024 * 1024; // 1GB default
    server->max_concurrent_connections = 10;
    
    g_mock_ftp_server_count++;
    
    if (g_detailed_logging) {
        printf("Mock FTP server created: %s:%d\n", host, port);
    }
    
    return server;
}

/**
 * Find a mock FTP server
 */
mock_ftp_server_t* mock_ftp_find_server(const char* host, uint16_t port) {
    if (!host) return NULL;
    
    for (size_t i = 0; i < g_mock_ftp_server_count; i++) {
        if (strcmp(g_mock_ftp_servers[i].host, host) == 0 &&
            g_mock_ftp_servers[i].port == port) {
            return &g_mock_ftp_servers[i];
        }
    }
    
    return NULL;
}

/**
 * Set server availability
 */
void mock_ftp_set_server_availability(mock_ftp_server_t* server, bool available) {
    if (server) {
        server->is_available = available;
        if (g_detailed_logging) {
            printf("Server %s:%d availability set to %s\n", 
                   server->host, server->port, available ? "true" : "false");
        }
    }
}

/**
 * Set failure rates for server
 */
void mock_ftp_set_failure_rates(mock_ftp_server_t* server, 
                               double connection_rate, double upload_rate, double download_rate) {
    if (server) {
        server->connection_failure_rate = connection_rate;
        server->upload_failure_rate = upload_rate;
        server->download_failure_rate = download_rate;
        
        if (g_detailed_logging) {
            printf("Server %s:%d failure rates set: conn=%.2f, up=%.2f, down=%.2f\n",
                   server->host, server->port, connection_rate, upload_rate, download_rate);
        }
    }
}

/**
 * Set latency for server
 */
void mock_ftp_set_latency(mock_ftp_server_t* server, uint32_t min_ms, uint32_t max_ms) {
    if (server) {
        server->latency_ms_min = min_ms;
        server->latency_ms_max = max_ms;
        
        if (g_detailed_logging) {
            printf("Server %s:%d latency set: %u-%u ms\n",
                   server->host, server->port, min_ms, max_ms);
        }
    }
}

/**
 * Clear all files from server
 */
void mock_ftp_server_clear_files(mock_ftp_server_t* server) {
    if (!server) return;
    
    for (size_t i = 0; i < server->file_count; i++) {
        if (server->files[i].data) {
            free(server->files[i].data);
            server->files[i].data = NULL;
        }
    }
    
    server->file_count = 0;
    server->storage_used = 0;
}

/**
 * Initialize mock FTP client
 */
mock_ftp_client_context_t* mock_ftp_client_init(void) {
    mock_ftp_client_context_t* ctx = malloc(sizeof(mock_ftp_client_context_t));
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(mock_ftp_client_context_t));
    ctx->timeout_ms = 30000; // 30 second default timeout
    ctx->passive_mode = true;
    
    return ctx;
}

/**
 * Cleanup mock FTP client
 */
void mock_ftp_client_cleanup(mock_ftp_client_context_t* ctx) {
    if (ctx) {
        if (ctx->is_connected) {
            mock_ftp_disconnect(ctx);
        }
        free(ctx);
    }
}

/**
 * Connect to mock FTP server
 */
int mock_ftp_connect(mock_ftp_client_context_t* ctx, const char* host, uint16_t port,
                    const char* username, const char* password) {
    if (!ctx || !host || !username || !password) {
        if (ctx) strncpy(ctx->last_error, "Invalid parameters", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_CONNECTION_FAILED;
    }
    
    // Find server
    mock_ftp_server_t* server = mock_ftp_find_server(host, port);
    if (!server) {
        snprintf(ctx->last_error, sizeof(ctx->last_error), "Server not found: %s:%d", host, port);
        return MOCK_FTP_ERROR_CONNECTION_FAILED;
    }
    
    // Check server availability
    if (!server->is_available) {
        strncpy(ctx->last_error, "Server unavailable", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_SERVER_UNAVAILABLE;
    }
    
    // Simulate connection failure
    if (mock_ftp_random_double() < server->connection_failure_rate) {
        strncpy(ctx->last_error, "Connection failed (simulated)", sizeof(ctx->last_error) - 1);
        server->failed_uploads++; // Count as failed operation
        return MOCK_FTP_ERROR_CONNECTION_FAILED;
    }
    
    // Check authentication
    if (strcmp(server->username, username) != 0 || strcmp(server->password, password) != 0) {
        strncpy(ctx->last_error, "Authentication failed", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_AUTH_FAILED;
    }
    
    // Simulate latency
    mock_ftp_simulate_delay(server->latency_ms_min, server->latency_ms_max);
    
    // Connection successful
    ctx->connected_server = server;
    ctx->is_connected = true;
    server->current_connections++;
    
    if (g_detailed_logging) {
        printf("Mock FTP connected to %s:%d as %s\n", host, port, username);
    }
    
    return MOCK_FTP_SUCCESS;
}

/**
 * Disconnect from mock FTP server
 */
int mock_ftp_disconnect(mock_ftp_client_context_t* ctx) {
    if (!ctx) return MOCK_FTP_ERROR_CONNECTION_FAILED;
    
    if (ctx->is_connected && ctx->connected_server) {
        ctx->connected_server->current_connections--;
        
        if (g_detailed_logging) {
            printf("Mock FTP disconnected from %s:%d\n", 
                   ctx->connected_server->host, ctx->connected_server->port);
        }
    }
    
    ctx->connected_server = NULL;
    ctx->is_connected = false;
    
    return MOCK_FTP_SUCCESS;
}

/**
 * Upload data to mock FTP server
 */
int mock_ftp_upload_data(mock_ftp_client_context_t* ctx, const uint8_t* data, size_t size,
                        const char* remote_path) {
    if (!ctx || !ctx->is_connected || !ctx->connected_server || !data || !remote_path) {
        if (ctx) strncpy(ctx->last_error, "Not connected or invalid parameters", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_UPLOAD_FAILED;
    }
    
    mock_ftp_server_t* server = ctx->connected_server;
    
    // Simulate upload failure
    if (mock_ftp_random_double() < server->upload_failure_rate) {
        strncpy(ctx->last_error, "Upload failed (simulated)", sizeof(ctx->last_error) - 1);
        server->failed_uploads++;
        return MOCK_FTP_ERROR_UPLOAD_FAILED;
    }
    
    // Check storage capacity
    if (server->storage_used + size > server->storage_capacity) {
        strncpy(ctx->last_error, "Storage full", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_STORAGE_FULL;
    }
    
    // Check if we have space for another file
    if (server->file_count >= MOCK_FTP_MAX_FILES_PER_SERVER) {
        strncpy(ctx->last_error, "Too many files", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_STORAGE_FULL;
    }
    
    // Simulate latency
    mock_ftp_simulate_delay(server->latency_ms_min, server->latency_ms_max);
    
    // Find existing file or create new one
    mock_ftp_file_t* file = NULL;
    for (size_t i = 0; i < server->file_count; i++) {
        if (strcmp(server->files[i].filename, remote_path) == 0) {
            file = &server->files[i];
            // Free existing data
            if (file->data) {
                server->storage_used -= file->size;
                free(file->data);
            }
            break;
        }
    }
    
    // Create new file entry if not found
    if (!file) {
        file = &server->files[server->file_count];
        server->file_count++;
        strncpy(file->filename, remote_path, sizeof(file->filename) - 1);
    }
    
    // Allocate and copy data
    file->data = malloc(size);
    if (!file->data) {
        strncpy(ctx->last_error, "Memory allocation failed", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_UPLOAD_FAILED;
    }
    
    memcpy(file->data, data, size);
    file->size = size;
    file->created_time = time(NULL);
    file->modified_time = file->created_time;
    file->is_corrupted = false;
    
    // Update server statistics
    server->storage_used += size;
    server->total_uploads++;
    server->bytes_uploaded += size;
    
    if (g_detailed_logging) {
        printf("Mock FTP uploaded %zu bytes to %s:%d%s\n", 
               size, server->host, server->port, remote_path);
    }
    
    return MOCK_FTP_SUCCESS;
}

/**
 * Download data from mock FTP server
 */
int mock_ftp_download_data(mock_ftp_client_context_t* ctx, const char* remote_path,
                          uint8_t** data, size_t* size) {
    if (!ctx || !ctx->is_connected || !ctx->connected_server || !remote_path || !data || !size) {
        if (ctx) strncpy(ctx->last_error, "Not connected or invalid parameters", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_DOWNLOAD_FAILED;
    }
    
    mock_ftp_server_t* server = ctx->connected_server;
    
    // Simulate download failure
    if (mock_ftp_random_double() < server->download_failure_rate) {
        strncpy(ctx->last_error, "Download failed (simulated)", sizeof(ctx->last_error) - 1);
        server->failed_downloads++;
        return MOCK_FTP_ERROR_DOWNLOAD_FAILED;
    }
    
    // Find file
    mock_ftp_file_t* file = NULL;
    for (size_t i = 0; i < server->file_count; i++) {
        if (strcmp(server->files[i].filename, remote_path) == 0) {
            file = &server->files[i];
            break;
        }
    }
    
    if (!file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error), "File not found: %s", remote_path);
        return MOCK_FTP_ERROR_FILE_NOT_FOUND;
    }
    
    // Check for corruption
    if (file->is_corrupted) {
        strncpy(ctx->last_error, "File corrupted", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_CORRUPTED_DATA;
    }
    
    // Simulate latency
    mock_ftp_simulate_delay(server->latency_ms_min, server->latency_ms_max);
    
    // Allocate and copy data
    *data = malloc(file->size);
    if (!*data) {
        strncpy(ctx->last_error, "Memory allocation failed", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_DOWNLOAD_FAILED;
    }
    
    memcpy(*data, file->data, file->size);
    *size = file->size;
    
    // Update server statistics
    server->total_downloads++;
    server->bytes_downloaded += file->size;
    
    if (g_detailed_logging) {
        printf("Mock FTP downloaded %zu bytes from %s:%d%s\n", 
               file->size, server->host, server->port, remote_path);
    }
    
    return MOCK_FTP_SUCCESS;
}

/**
 * Delete file from mock FTP server
 */
int mock_ftp_delete_file(mock_ftp_client_context_t* ctx, const char* remote_path) {
    if (!ctx || !ctx->is_connected || !ctx->connected_server || !remote_path) {
        if (ctx) strncpy(ctx->last_error, "Not connected or invalid parameters", sizeof(ctx->last_error) - 1);
        return MOCK_FTP_ERROR_UPLOAD_FAILED;
    }
    
    mock_ftp_server_t* server = ctx->connected_server;
    
    // Find and remove file
    for (size_t i = 0; i < server->file_count; i++) {
        if (strcmp(server->files[i].filename, remote_path) == 0) {
            // Free data
            if (server->files[i].data) {
                server->storage_used -= server->files[i].size;
                free(server->files[i].data);
            }
            
            // Move last file to this position
            if (i < server->file_count - 1) {
                server->files[i] = server->files[server->file_count - 1];
            }
            
            server->file_count--;
            
            if (g_detailed_logging) {
                printf("Mock FTP deleted %s from %s:%d\n", 
                       remote_path, server->host, server->port);
            }
            
            return MOCK_FTP_SUCCESS;
        }
    }
    
    snprintf(ctx->last_error, sizeof(ctx->last_error), "File not found: %s", remote_path);
    return MOCK_FTP_ERROR_FILE_NOT_FOUND;
}

/**
 * Check if file exists on mock FTP server
 */
bool mock_ftp_file_exists(mock_ftp_client_context_t* ctx, const char* remote_path) {
    if (!ctx || !ctx->is_connected || !ctx->connected_server || !remote_path) {
        return false;
    }
    
    mock_ftp_server_t* server = ctx->connected_server;
    
    for (size_t i = 0; i < server->file_count; i++) {
        if (strcmp(server->files[i].filename, remote_path) == 0) {
            return true;
        }
    }
    
    return false;
}

/**
 * Get error string for mock FTP result
 */
const char* mock_ftp_get_error_string(mock_ftp_result_t result) {
    switch (result) {
        case MOCK_FTP_SUCCESS: return "Success";
        case MOCK_FTP_ERROR_CONNECTION_FAILED: return "Connection failed";
        case MOCK_FTP_ERROR_AUTH_FAILED: return "Authentication failed";
        case MOCK_FTP_ERROR_FILE_NOT_FOUND: return "File not found";
        case MOCK_FTP_ERROR_UPLOAD_FAILED: return "Upload failed";
        case MOCK_FTP_ERROR_DOWNLOAD_FAILED: return "Download failed";
        case MOCK_FTP_ERROR_STORAGE_FULL: return "Storage full";
        case MOCK_FTP_ERROR_TIMEOUT: return "Timeout";
        case MOCK_FTP_ERROR_NETWORK: return "Network error";
        case MOCK_FTP_ERROR_SERVER_UNAVAILABLE: return "Server unavailable";
        case MOCK_FTP_ERROR_CORRUPTED_DATA: return "Corrupted data";
        default: return "Unknown error";
    }
}

/**
 * Test scenario setup helpers
 */
int mock_ftp_setup_test_scenario_basic(void) {
    mock_ftp_init();
    
    // Create 3 basic servers
    mock_ftp_create_server("server1.test", 21, "test", "test");
    mock_ftp_create_server("server2.test", 21, "test", "test");
    mock_ftp_create_server("server3.test", 21, "test", "test");
    
    if (g_detailed_logging) {
        printf("Basic test scenario setup complete\n");
    }
    
    return 0;
}

/**
 * Enable detailed logging
 */
void mock_ftp_enable_detailed_logging(bool enable) {
    g_detailed_logging = enable;
}
