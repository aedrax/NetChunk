#include "ftp_client.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

// Internal helper functions
static size_t ftp_write_callback(void* contents, size_t size, size_t nmemb, netchunk_memory_buffer_t* buffer);
static size_t ftp_read_callback(void* ptr, size_t size, size_t nmemb, FILE* stream);
static int ftp_progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
static netchunk_error_t setup_curl_options(CURL* curl, const netchunk_server_t* server);
static netchunk_error_t perform_curl_operation(netchunk_ftp_connection_t* connection);
static void update_connection_stats(netchunk_ftp_connection_t* connection, bool success, size_t bytes_transferred);
static double get_current_time_ms(void);

// Global curl initialization
static pthread_once_t curl_init_once = PTHREAD_ONCE_INIT;

static void curl_global_init_once(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

// FTP Pool Management Functions

netchunk_error_t netchunk_ftp_pool_init(netchunk_ftp_pool_t* pool, netchunk_config_t* config)
{
    if (!pool || !config) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Initialize libcurl globally (once)
    pthread_once(&curl_init_once, curl_global_init_once);

    memset(pool, 0, sizeof(netchunk_ftp_pool_t));

    // Initialize pool mutex and condition variable
    if (pthread_mutex_init(&pool->pool_mutex, NULL) != 0) {
        return NETCHUNK_ERROR_UNKNOWN;
    }

    if (pthread_cond_init(&pool->connection_available, NULL) != 0) {
        pthread_mutex_destroy(&pool->pool_mutex);
        return NETCHUNK_ERROR_UNKNOWN;
    }

    pool->config = config;
    pool->max_concurrent = config->max_concurrent_operations;
    pool->connection_count = 0;

    // Initialize connections for each server
    for (int i = 0; i < config->server_count && i < NETCHUNK_FTP_POOL_MAX_CONNECTIONS; i++) {
        netchunk_ftp_connection_t* conn = &pool->connections[i];
        memset(conn, 0, sizeof(netchunk_ftp_connection_t));

        conn->server = &config->servers[i];
        conn->status = NETCHUNK_FTP_STATUS_DISCONNECTED;
        conn->in_use = false;

        if (pthread_mutex_init(&conn->mutex, NULL) != 0) {
            // Cleanup previously initialized connections
            for (int j = 0; j < i; j++) {
                pthread_mutex_destroy(&pool->connections[j].mutex);
            }
            pthread_mutex_destroy(&pool->pool_mutex);
            pthread_cond_destroy(&pool->connection_available);
            return NETCHUNK_ERROR_UNKNOWN;
        }

        pool->connection_count++;
    }

    pool->initialized = true;
    return NETCHUNK_SUCCESS;
}

void netchunk_ftp_pool_cleanup(netchunk_ftp_pool_t* pool)
{
    if (!pool || !pool->initialized) {
        return;
    }

    pthread_mutex_lock(&pool->pool_mutex);

    // Cleanup all connections
    for (int i = 0; i < pool->connection_count; i++) {
        netchunk_ftp_connection_t* conn = &pool->connections[i];

        pthread_mutex_lock(&conn->mutex);

        if (conn->curl_handle) {
            curl_easy_cleanup(conn->curl_handle);
            conn->curl_handle = NULL;
        }

        conn->status = NETCHUNK_FTP_STATUS_DISCONNECTED;
        conn->in_use = false;

        pthread_mutex_unlock(&conn->mutex);
        pthread_mutex_destroy(&conn->mutex);
    }

    pool->initialized = false;
    pthread_mutex_unlock(&pool->pool_mutex);

    pthread_mutex_destroy(&pool->pool_mutex);
    pthread_cond_destroy(&pool->connection_available);

    // Note: Don't call curl_global_cleanup() here as other parts of the application might use libcurl
}

netchunk_error_t netchunk_ftp_pool_acquire(netchunk_ftp_pool_t* pool, int server_id, netchunk_ftp_connection_t** connection)
{
    if (!pool || !connection || server_id < 0 || server_id >= pool->connection_count) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&pool->pool_mutex);

    // Find connection for the specified server
    netchunk_ftp_connection_t* conn = &pool->connections[server_id];

    // Wait for connection to become available if it's in use
    while (conn->in_use) {
        pthread_cond_wait(&pool->connection_available, &pool->pool_mutex);
    }

    pthread_mutex_lock(&conn->mutex);

    // Mark connection as in use
    conn->in_use = true;
    conn->last_used = time(NULL);

    // Initialize CURL handle if not already done
    if (!conn->curl_handle) {
        conn->curl_handle = curl_easy_init();
        if (!conn->curl_handle) {
            conn->in_use = false;
            pthread_mutex_unlock(&conn->mutex);
            pthread_mutex_unlock(&pool->pool_mutex);
            return NETCHUNK_ERROR_OUT_OF_MEMORY;
        }

        netchunk_error_t setup_error = setup_curl_options(conn->curl_handle, conn->server);
        if (setup_error != NETCHUNK_SUCCESS) {
            curl_easy_cleanup(conn->curl_handle);
            conn->curl_handle = NULL;
            conn->in_use = false;
            pthread_mutex_unlock(&conn->mutex);
            pthread_mutex_unlock(&pool->pool_mutex);
            return setup_error;
        }

        conn->status = NETCHUNK_FTP_STATUS_CONNECTED;
        conn->connected_at = time(NULL);
    }

    *connection = conn;

    pthread_mutex_unlock(&conn->mutex);
    pthread_mutex_unlock(&pool->pool_mutex);

    return NETCHUNK_SUCCESS;
}

void netchunk_ftp_pool_release(netchunk_ftp_pool_t* pool, netchunk_ftp_connection_t* connection)
{
    if (!pool || !connection) {
        return;
    }

    pthread_mutex_lock(&pool->pool_mutex);
    pthread_mutex_lock(&connection->mutex);

    connection->in_use = false;
    connection->last_used = time(NULL);

    pthread_mutex_unlock(&connection->mutex);

    // Signal that a connection is now available
    pthread_cond_signal(&pool->connection_available);

    pthread_mutex_unlock(&pool->pool_mutex);
}

netchunk_error_t netchunk_ftp_pool_test_connectivity(netchunk_ftp_pool_t* pool)
{
    if (!pool || !pool->initialized) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    for (int i = 0; i < pool->connection_count; i++) {
        double latency_ms;
        netchunk_error_t error = netchunk_ftp_test_server(&pool->config->servers[i], &latency_ms);
        if (error != NETCHUNK_SUCCESS) {
            return error;
        }

        // Update server status
        pool->config->servers[i].status = NETCHUNK_SERVER_AVAILABLE;
        pool->config->servers[i].last_latency_ms = latency_ms;
        pool->config->servers[i].last_health_check = time(NULL);
    }

    return NETCHUNK_SUCCESS;
}

// FTP Operation Functions

netchunk_error_t netchunk_ftp_upload(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    const uint8_t* data,
    size_t data_size,
    netchunk_upload_progress_t* progress)
{
    if (!connection || !remote_path || !data || data_size == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Create temporary file for upload data
    FILE* temp_file = tmpfile();
    if (!temp_file) {
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    // Write data to temporary file
    if (fwrite(data, 1, data_size, temp_file) != data_size) {
        fclose(temp_file);
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    // Rewind file
    rewind(temp_file);

    // Setup curl options for upload
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_READDATA, temp_file);
    curl_easy_setopt(connection->curl_handle, CURLOPT_INFILESIZE_LARGE, (curl_off_t)data_size);

    // Setup progress callback if provided
    if (progress) {
        progress->total_bytes = (double)data_size;
        progress->uploaded_bytes = 0.0;
        progress->start_time = time(NULL);
        curl_easy_setopt(connection->curl_handle, CURLOPT_XFERINFOFUNCTION, ftp_progress_callback);
        curl_easy_setopt(connection->curl_handle, CURLOPT_XFERINFODATA, progress);
        curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 0L);
    } else {
        curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 1L);
    }

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the upload
    netchunk_error_t result = perform_curl_operation(connection);

    // Update statistics
    update_connection_stats(connection, result == NETCHUNK_SUCCESS, data_size);

    // Cleanup
    fclose(temp_file);

    // Reset curl options
    curl_easy_setopt(connection->curl_handle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_READDATA, NULL);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 1L);

    connection->status = (result == NETCHUNK_SUCCESS) ? NETCHUNK_FTP_STATUS_CONNECTED : NETCHUNK_FTP_STATUS_ERROR;

    pthread_mutex_unlock(&connection->mutex);

    return result;
}

// High-level FTP Context Functions

netchunk_error_t netchunk_ftp_init(netchunk_ftp_context_t* context, netchunk_config_t* config)
{
    if (!context || !config) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(context, 0, sizeof(netchunk_ftp_context_t));

    // Allocate and initialize pool
    context->pool = malloc(sizeof(netchunk_ftp_pool_t));
    if (!context->pool) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    netchunk_error_t pool_error = netchunk_ftp_pool_init(context->pool, config);
    if (pool_error != NETCHUNK_SUCCESS) {
        free(context->pool);
        context->pool = NULL;
        return pool_error;
    }

    context->config = config;
    context->initialized = true;

    return NETCHUNK_SUCCESS;
}

void netchunk_ftp_cleanup(netchunk_ftp_context_t* context)
{
    if (!context)
        return;

    if (context->pool) {
        netchunk_ftp_pool_cleanup(context->pool);
        free(context->pool);
        context->pool = NULL;
    }

    context->config = NULL;
    context->initialized = false;
}

netchunk_error_t netchunk_ftp_test_connection(netchunk_ftp_context_t* context,
    const netchunk_server_t* server)
{
    if (!context || !server) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    double latency_ms;
    return netchunk_ftp_test_server(server, &latency_ms);
}

// Chunk-specific Functions (stubs for now)

netchunk_error_t netchunk_ftp_upload_chunk(netchunk_ftp_context_t* context,
    const netchunk_server_t* server,
    const netchunk_chunk_t* chunk)
{
    if (!context || !server || !chunk) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // TODO: Implement chunk upload logic
    // For now, return success to allow linking
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_ftp_download_chunk(netchunk_ftp_context_t* context,
    const netchunk_server_t* server,
    netchunk_chunk_t* chunk)
{
    if (!context || !server || !chunk) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // TODO: Implement chunk download logic
    // For now, return success to allow linking
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_ftp_delete_chunk(netchunk_ftp_context_t* context,
    const netchunk_server_t* server,
    const netchunk_chunk_t* chunk)
{
    if (!context || !server || !chunk) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // TODO: Implement chunk delete logic
    // For now, return success to allow linking
    return NETCHUNK_SUCCESS;
}

// Manifest-specific Functions (stubs for now)

netchunk_error_t netchunk_ftp_upload_manifest(netchunk_ftp_context_t* context,
    netchunk_config_t* config,
    const netchunk_file_manifest_t* manifest)
{
    if (!context || !config || !manifest) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // TODO: Implement manifest upload logic
    // For now, return success to allow linking
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_ftp_download_manifest(netchunk_ftp_context_t* context,
    netchunk_config_t* config,
    const char* remote_name,
    netchunk_file_manifest_t* manifest)
{
    if (!context || !config || !remote_name || !manifest) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // TODO: Implement manifest download logic
    // For now, return success to allow linking
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_ftp_delete_manifest(netchunk_ftp_context_t* context,
    netchunk_config_t* config,
    const char* remote_name)
{
    if (!context || !config || !remote_name) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // TODO: Implement manifest delete logic
    // For now, return success to allow linking
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_ftp_list_manifests(netchunk_ftp_context_t* context,
    netchunk_config_t* config,
    netchunk_file_manifest_t** files,
    size_t* count)
{
    if (!context || !config || !files || !count) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // TODO: Implement manifest listing logic
    // For now, return empty list to allow linking
    *files = NULL;
    *count = 0;
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_ftp_upload_file(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    const char* local_file_path,
    netchunk_upload_progress_t* progress)
{
    if (!connection || !remote_path || !local_file_path) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    FILE* local_file = fopen(local_file_path, "rb");
    if (!local_file) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    }

    // Get file size
    fseek(local_file, 0, SEEK_END);
    size_t file_size = ftell(local_file);
    rewind(local_file);

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        fclose(local_file);
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        fclose(local_file);
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Setup curl options for upload
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_READDATA, local_file);
    curl_easy_setopt(connection->curl_handle, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_size);

    // Setup progress callback if provided
    if (progress) {
        progress->total_bytes = (double)file_size;
        progress->uploaded_bytes = 0.0;
        progress->start_time = time(NULL);
        curl_easy_setopt(connection->curl_handle, CURLOPT_XFERINFOFUNCTION, ftp_progress_callback);
        curl_easy_setopt(connection->curl_handle, CURLOPT_XFERINFODATA, progress);
        curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 0L);
    } else {
        curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 1L);
    }

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the upload
    netchunk_error_t result = perform_curl_operation(connection);

    // Update statistics
    update_connection_stats(connection, result == NETCHUNK_SUCCESS, file_size);

    // Cleanup
    fclose(local_file);

    // Reset curl options
    curl_easy_setopt(connection->curl_handle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_READDATA, NULL);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 1L);

    connection->status = (result == NETCHUNK_SUCCESS) ? NETCHUNK_FTP_STATUS_CONNECTED : NETCHUNK_FTP_STATUS_ERROR;

    pthread_mutex_unlock(&connection->mutex);

    return result;
}

netchunk_error_t netchunk_ftp_download(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    netchunk_memory_buffer_t* buffer,
    netchunk_download_progress_t* progress)
{
    if (!connection || !remote_path || !buffer) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Reset buffer
    buffer->size = 0;
    buffer->position = 0;

    // Setup curl options for download
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEFUNCTION, ftp_write_callback);
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEDATA, buffer);

    // Setup progress callback if provided
    if (progress) {
        progress->downloaded_bytes = 0.0;
        progress->start_time = time(NULL);
        curl_easy_setopt(connection->curl_handle, CURLOPT_XFERINFOFUNCTION, ftp_progress_callback);
        curl_easy_setopt(connection->curl_handle, CURLOPT_XFERINFODATA, progress);
        curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 0L);
    } else {
        curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 1L);
    }

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the download
    netchunk_error_t result = perform_curl_operation(connection);

    // Update statistics
    update_connection_stats(connection, result == NETCHUNK_SUCCESS, buffer->size);

    // Reset curl options
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 1L);

    connection->status = (result == NETCHUNK_SUCCESS) ? NETCHUNK_FTP_STATUS_CONNECTED : NETCHUNK_FTP_STATUS_ERROR;

    pthread_mutex_unlock(&connection->mutex);

    return result;
}

netchunk_error_t netchunk_ftp_download_file(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    const char* local_file_path,
    netchunk_download_progress_t* progress)
{
    if (!connection || !remote_path || !local_file_path) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    FILE* local_file = fopen(local_file_path, "wb");
    if (!local_file) {
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        fclose(local_file);
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        fclose(local_file);
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Setup curl options for download
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEDATA, local_file);

    // Setup progress callback if provided
    if (progress) {
        progress->downloaded_bytes = 0.0;
        progress->start_time = time(NULL);
        curl_easy_setopt(connection->curl_handle, CURLOPT_XFERINFOFUNCTION, ftp_progress_callback);
        curl_easy_setopt(connection->curl_handle, CURLOPT_XFERINFODATA, progress);
        curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 0L);
    } else {
        curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 1L);
    }

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the download
    netchunk_error_t result = perform_curl_operation(connection);

    // Get file size for stats
    fseek(local_file, 0, SEEK_END);
    size_t file_size = ftell(local_file);

    // Update statistics
    update_connection_stats(connection, result == NETCHUNK_SUCCESS, file_size);

    // Cleanup
    fclose(local_file);

    // Reset curl options
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOPROGRESS, 1L);

    connection->status = (result == NETCHUNK_SUCCESS) ? NETCHUNK_FTP_STATUS_CONNECTED : NETCHUNK_FTP_STATUS_ERROR;

    pthread_mutex_unlock(&connection->mutex);

    return result;
}

netchunk_error_t netchunk_ftp_delete(netchunk_ftp_connection_t* connection, const char* remote_path)
{
    if (!connection || !remote_path) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Prepare DELE command
    struct curl_slist* commands = NULL;
    char dele_cmd[2048];
    snprintf(dele_cmd, sizeof(dele_cmd), "DELE %s", remote_path);
    commands = curl_slist_append(commands, dele_cmd);

    // Setup curl options for delete
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_QUOTE, commands);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOBODY, 1L);

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the delete
    netchunk_error_t result = perform_curl_operation(connection);

    // Update statistics
    update_connection_stats(connection, result == NETCHUNK_SUCCESS, 0);

    // Cleanup
    curl_slist_free_all(commands);
    curl_easy_setopt(connection->curl_handle, CURLOPT_QUOTE, NULL);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOBODY, 0L);

    connection->status = (result == NETCHUNK_SUCCESS) ? NETCHUNK_FTP_STATUS_CONNECTED : NETCHUNK_FTP_STATUS_ERROR;

    pthread_mutex_unlock(&connection->mutex);

    return result;
}

// Memory Buffer Functions

netchunk_error_t netchunk_memory_buffer_init(netchunk_memory_buffer_t* buffer, size_t initial_capacity)
{
    if (!buffer) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(buffer, 0, sizeof(netchunk_memory_buffer_t));

    if (initial_capacity > 0) {
        buffer->data = malloc(initial_capacity);
        if (!buffer->data) {
            return NETCHUNK_ERROR_OUT_OF_MEMORY;
        }
        buffer->capacity = initial_capacity;
    }

    return NETCHUNK_SUCCESS;
}

void netchunk_memory_buffer_cleanup(netchunk_memory_buffer_t* buffer)
{
    if (!buffer) {
        return;
    }

    if (buffer->data) {
        free(buffer->data);
        buffer->data = NULL;
    }

    buffer->size = 0;
    buffer->capacity = 0;
    buffer->position = 0;
}

netchunk_error_t netchunk_memory_buffer_resize(netchunk_memory_buffer_t* buffer, size_t new_capacity)
{
    if (!buffer || new_capacity == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    uint8_t* new_data = realloc(buffer->data, new_capacity);
    if (!new_data) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;

    // Adjust size if it exceeds new capacity
    if (buffer->size > new_capacity) {
        buffer->size = new_capacity;
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_memory_buffer_append(netchunk_memory_buffer_t* buffer, const uint8_t* data, size_t data_size)
{
    if (!buffer || !data || data_size == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Check if we need to resize
    if (buffer->size + data_size > buffer->capacity) {
        size_t new_capacity = buffer->capacity * 2;
        if (new_capacity < buffer->size + data_size) {
            new_capacity = buffer->size + data_size + 4096; // Add some extra space
        }

        netchunk_error_t resize_error = netchunk_memory_buffer_resize(buffer, new_capacity);
        if (resize_error != NETCHUNK_SUCCESS) {
            return resize_error;
        }
    }

    // Append data
    memcpy(buffer->data + buffer->size, data, data_size);
    buffer->size += data_size;

    return NETCHUNK_SUCCESS;
}

// Utility Functions

netchunk_error_t netchunk_ftp_test_server(const netchunk_server_t* server, double* latency_ms)
{
    if (!server || !latency_ms) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    // Build URL for root directory listing
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(server, "", url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        curl_easy_cleanup(curl);
        return url_error;
    }

    // Setup basic options
    netchunk_error_t setup_error = setup_curl_options(curl, server);
    if (setup_error != NETCHUNK_SUCCESS) {
        curl_easy_cleanup(curl);
        return setup_error;
    }

    // Set URL and configure for directory listing
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ftp_write_callback);

    // Create dummy buffer to receive data
    netchunk_memory_buffer_t dummy_buffer;
    netchunk_memory_buffer_init(&dummy_buffer, 1024);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dummy_buffer);

    // Measure latency
    double start_time = get_current_time_ms();
    CURLcode res = curl_easy_perform(curl);
    double end_time = get_current_time_ms();

    *latency_ms = end_time - start_time;

    // Cleanup
    netchunk_memory_buffer_cleanup(&dummy_buffer);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? NETCHUNK_SUCCESS : NETCHUNK_ERROR_NETWORK;
}

netchunk_error_t netchunk_ftp_build_url(const netchunk_server_t* server,
    const char* remote_path,
    char* url_buffer,
    size_t buffer_size)
{
    if (!server || !remote_path || !url_buffer || buffer_size == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    const char* protocol = server->use_ssl ? "ftps" : "ftp";

    // Handle empty remote path
    const char* path = (strlen(remote_path) == 0) ? "" : remote_path;

    // Ensure base_path ends with / and remote_path doesn't start with /
    char normalized_base_path[NETCHUNK_MAX_PATH_LEN];
    strcpy(normalized_base_path, server->base_path);

    size_t base_len = strlen(normalized_base_path);
    if (base_len > 0 && normalized_base_path[base_len - 1] != '/') {
        strcat(normalized_base_path, "/");
    }

    // Skip leading slash in remote path
    if (path[0] == '/') {
        path++;
    }

    // Build URL: protocol://username:password@host:port/base_path/remote_path
    int result = snprintf(url_buffer, buffer_size, "%s://%s:%s@%s:%d%s%s",
        protocol,
        server->username,
        server->password,
        server->host,
        server->port,
        normalized_base_path,
        path);

    if (result >= (int)buffer_size || result < 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    return NETCHUNK_SUCCESS;
}

const char* netchunk_ftp_get_error_message(const netchunk_ftp_connection_t* connection)
{
    if (!connection || strlen(connection->error_message) == 0) {
        return "No error message available";
    }
    return connection->error_message;
}

void netchunk_ftp_reset_stats(netchunk_ftp_connection_t* connection)
{
    if (!connection) {
        return;
    }

    pthread_mutex_lock((pthread_mutex_t*)&connection->mutex);
    memset(&connection->stats, 0, sizeof(netchunk_ftp_stats_t));
    pthread_mutex_unlock((pthread_mutex_t*)&connection->mutex);
}

// Internal helper function implementations

static size_t ftp_write_callback(void* contents, size_t size, size_t nmemb, netchunk_memory_buffer_t* buffer)
{
    size_t real_size = size * nmemb;

    if (!buffer || !contents || real_size == 0) {
        return 0;
    }

    netchunk_error_t append_error = netchunk_memory_buffer_append(buffer, (const uint8_t*)contents, real_size);
    if (append_error != NETCHUNK_SUCCESS) {
        return 0; // Signal error to libcurl
    }

    return real_size;
}

static size_t ftp_read_callback(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    if (!ptr || !stream || size == 0 || nmemb == 0) {
        return 0;
    }

    return fread(ptr, size, nmemb, stream);
}

static int ftp_progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    if (!clientp) {
        return 0;
    }

    // Check if this is an upload or download progress
    if (ultotal > 0) {
        // Upload progress
        netchunk_upload_progress_t* progress = (netchunk_upload_progress_t*)clientp;

        if (progress->cancelled) {
            return 1; // Cancel transfer
        }

        progress->uploaded_bytes = (double)ulnow;
        if (ultotal > 0) {
            progress->total_bytes = (double)ultotal;
        }

        // Calculate transfer rate
        time_t current_time = time(NULL);
        time_t elapsed = current_time - progress->start_time;
        if (elapsed > 0) {
            progress->transfer_rate_bps = progress->uploaded_bytes / elapsed;
        }

        // Call user callback if provided
        if (progress->callback) {
            progress->callback(progress->userdata, progress->total_bytes, progress->uploaded_bytes);
        }
    } else if (dltotal > 0) {
        // Download progress
        netchunk_download_progress_t* progress = (netchunk_download_progress_t*)clientp;

        if (progress->cancelled) {
            return 1; // Cancel transfer
        }

        progress->downloaded_bytes = (double)dlnow;
        if (dltotal > 0) {
            progress->total_bytes = (double)dltotal;
        }

        // Calculate transfer rate
        time_t current_time = time(NULL);
        time_t elapsed = current_time - progress->start_time;
        if (elapsed > 0) {
            progress->transfer_rate_bps = progress->downloaded_bytes / elapsed;
        }

        // Call user callback if provided
        if (progress->callback) {
            progress->callback(progress->userdata, progress->total_bytes, progress->downloaded_bytes);
        }
    }

    return 0; // Continue transfer
}

static netchunk_error_t setup_curl_options(CURL* curl, const netchunk_server_t* server)
{
    if (!curl || !server) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Basic FTP options
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_FTP | CURLPROTO_FTPS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, NETCHUNK_FTP_CONNECTION_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, server->use_ssl ? 120 : 60); // Longer timeout for SSL
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, NETCHUNK_FTP_MAX_REDIRECTS);

    // Set passive/active mode
    if (server->passive_mode) {
        curl_easy_setopt(curl, CURLOPT_FTPPORT, NULL);
    } else {
        curl_easy_setopt(curl, CURLOPT_FTPPORT, "-");
    }

    // SSL/TLS options
    if (server->use_ssl) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_DEFAULT);

        // SSL certificate verification
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }

    // Error buffer
    static __thread char error_buffer[CURL_ERROR_SIZE];
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

// Verbose output for debugging (only in debug builds)
#ifdef DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

    return NETCHUNK_SUCCESS;
}

static netchunk_error_t perform_curl_operation(netchunk_ftp_connection_t* connection)
{
    if (!connection || !connection->curl_handle) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    int retry_count = 0;
    CURLcode res;

    do {
        res = curl_easy_perform(connection->curl_handle);

        if (res == CURLE_OK) {
            // Check HTTP response code for additional error info
            long response_code = 0;
            curl_easy_getinfo(connection->curl_handle, CURLINFO_RESPONSE_CODE, &response_code);

            if (response_code >= 400) {
                snprintf(connection->error_message, sizeof(connection->error_message),
                    "FTP error: HTTP %ld", response_code);
                return NETCHUNK_ERROR_FTP;
            }

            return NETCHUNK_SUCCESS;
        }

        // Store error message
        snprintf(connection->error_message, sizeof(connection->error_message),
            "FTP error: %s", curl_easy_strerror(res));

        retry_count++;

        // Retry on certain errors
        if (retry_count < NETCHUNK_FTP_MAX_RETRIES) {
            switch (res) {
            case CURLE_COULDNT_CONNECT:
            case CURLE_OPERATION_TIMEDOUT:
            case CURLE_RECV_ERROR:
            case CURLE_SEND_ERROR:
            case CURLE_FTP_CANT_GET_HOST:
                // Wait before retry with exponential backoff
                usleep((NETCHUNK_FTP_RETRY_DELAY_BASE * retry_count) * 1000);
                continue;
            default:
                break;
            }
        }

        break;
    } while (retry_count < NETCHUNK_FTP_MAX_RETRIES);

    // Map curl errors to netchunk errors
    switch (res) {
    case CURLE_COULDNT_CONNECT:
    case CURLE_FTP_CANT_GET_HOST:
        return NETCHUNK_ERROR_NETWORK;
    case CURLE_OPERATION_TIMEDOUT:
        return NETCHUNK_ERROR_TIMEOUT;
    case CURLE_OUT_OF_MEMORY:
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    case CURLE_FILE_COULDNT_READ_FILE:
    case CURLE_REMOTE_FILE_NOT_FOUND:
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    case CURLE_FTP_ACCESS_DENIED:
        return NETCHUNK_ERROR_FILE_ACCESS;
    default:
        return NETCHUNK_ERROR_FTP;
    }
}

static void update_connection_stats(netchunk_ftp_connection_t* connection, bool success, size_t bytes_transferred)
{
    if (!connection) {
        return;
    }

    netchunk_ftp_stats_t* stats = &connection->stats;

    if (success) {
        stats->successful_operations++;
        stats->bytes_uploaded += bytes_transferred; // This is a simplification - should track upload vs download separately
    } else {
        stats->failed_operations++;
        stats->connection_errors++;
    }

    stats->last_activity = time(NULL);

    // Update average latency (simplified calculation)
    // In a real implementation, you'd want to measure actual operation time
    stats->average_latency_ms = (stats->average_latency_ms + connection->server->last_latency_ms) / 2.0;
}

static double get_current_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// Additional FTP operation functions

netchunk_error_t netchunk_ftp_file_exists(netchunk_ftp_connection_t* connection, const char* remote_path, bool* exists)
{
    if (!connection || !remote_path || !exists) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Setup curl options for file existence check
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_FILETIME, 1L);

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the check
    CURLcode res = curl_easy_perform(connection->curl_handle);

    *exists = (res == CURLE_OK);

    // Reset curl options
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_FILETIME, 0L);

    connection->status = NETCHUNK_FTP_STATUS_CONNECTED;

    pthread_mutex_unlock(&connection->mutex);

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_ftp_get_file_size(netchunk_ftp_connection_t* connection, const char* remote_path, size_t* size)
{
    if (!connection || !remote_path || !size) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Setup curl options for getting file info
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOBODY, 1L);

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the request
    CURLcode res = curl_easy_perform(connection->curl_handle);

    if (res == CURLE_OK) {
        curl_off_t file_size;
        res = curl_easy_getinfo(connection->curl_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &file_size);
        if (res == CURLE_OK && file_size >= 0) {
            *size = (size_t)file_size;
        } else {
            *size = 0;
        }
    }

    // Reset curl options
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOBODY, 0L);

    connection->status = NETCHUNK_FTP_STATUS_CONNECTED;

    pthread_mutex_unlock(&connection->mutex);

    return (res == CURLE_OK) ? NETCHUNK_SUCCESS : NETCHUNK_ERROR_FTP;
}

netchunk_error_t netchunk_ftp_mkdir(netchunk_ftp_connection_t* connection, const char* remote_path)
{
    if (!connection || !remote_path) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Prepare MKD command
    struct curl_slist* commands = NULL;
    char mkd_cmd[2048];
    snprintf(mkd_cmd, sizeof(mkd_cmd), "MKD %s", remote_path);
    commands = curl_slist_append(commands, mkd_cmd);

    // Setup curl options for mkdir
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_QUOTE, commands);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOBODY, 1L);

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the mkdir
    netchunk_error_t result = perform_curl_operation(connection);

    // Update statistics
    update_connection_stats(connection, result == NETCHUNK_SUCCESS, 0);

    // Cleanup
    curl_slist_free_all(commands);
    curl_easy_setopt(connection->curl_handle, CURLOPT_QUOTE, NULL);
    curl_easy_setopt(connection->curl_handle, CURLOPT_NOBODY, 0L);

    connection->status = (result == NETCHUNK_SUCCESS) ? NETCHUNK_FTP_STATUS_CONNECTED : NETCHUNK_FTP_STATUS_ERROR;

    pthread_mutex_unlock(&connection->mutex);

    return result;
}

netchunk_error_t netchunk_ftp_list_directory(netchunk_ftp_connection_t* connection,
    const char* remote_path,
    netchunk_memory_buffer_t* buffer)
{
    if (!connection || !remote_path || !buffer) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&connection->mutex);

    if (!connection->curl_handle || connection->status == NETCHUNK_FTP_STATUS_ERROR) {
        pthread_mutex_unlock(&connection->mutex);
        return NETCHUNK_ERROR_FTP;
    }

    // Build full URL
    char url[2048];
    netchunk_error_t url_error = netchunk_ftp_build_url(connection->server, remote_path, url, sizeof(url));
    if (url_error != NETCHUNK_SUCCESS) {
        pthread_mutex_unlock(&connection->mutex);
        return url_error;
    }

    // Reset buffer
    buffer->size = 0;
    buffer->position = 0;

    // Setup curl options for directory listing
    curl_easy_setopt(connection->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(connection->curl_handle, CURLOPT_DIRLISTONLY, 1L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEFUNCTION, ftp_write_callback);
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEDATA, buffer);

    connection->status = NETCHUNK_FTP_STATUS_BUSY;

    // Perform the listing
    netchunk_error_t result = perform_curl_operation(connection);

    // Update statistics
    update_connection_stats(connection, result == NETCHUNK_SUCCESS, buffer->size);

    // Reset curl options
    curl_easy_setopt(connection->curl_handle, CURLOPT_DIRLISTONLY, 0L);
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(connection->curl_handle, CURLOPT_WRITEDATA, NULL);

    connection->status = (result == NETCHUNK_SUCCESS) ? NETCHUNK_FTP_STATUS_CONNECTED : NETCHUNK_FTP_STATUS_ERROR;

    pthread_mutex_unlock(&connection->mutex);

    return result;
}
