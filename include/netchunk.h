/**
 * @file netchunk.h
 * @brief NetChunk - Distributed File Storage System
 *
 * Main API for NetChunk distributed file storage system.
 * Provides reliable file storage by chunking files and distributing
 * across multiple FTP servers with configurable replication.
 */

#ifndef NETCHUNK_H
#define NETCHUNK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "chunker.h"
#include "config.h"
#include "crypto.h"
#include "ftp_client.h"
#include "manifest.h"

// Forward declaration for FTP context
typedef struct netchunk_ftp_context netchunk_ftp_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NetChunk version information
 */
#define NETCHUNK_VERSION_MAJOR 1
#define NETCHUNK_VERSION_MINOR 0
#define NETCHUNK_VERSION_PATCH 0
#define NETCHUNK_VERSION_STRING "1.0.0"

/**
 * @brief Progress callback function type
 *
 * @param userdata User-provided data pointer
 * @param operation_name Human-readable operation description
 * @param current Current progress units
 * @param total Total progress units
 * @param bytes_processed Bytes processed so far
 * @param bytes_total Total bytes to process
 */
typedef void (*netchunk_progress_callback_t)(
    void* userdata,
    const char* operation_name,
    uint64_t current,
    uint64_t total,
    uint64_t bytes_processed,
    uint64_t bytes_total);

/**
 * @brief Main NetChunk application context
 */
typedef struct netchunk_context {
    netchunk_config_t* config; // Configuration
    netchunk_ftp_context_t* ftp_context; // FTP client context
    netchunk_progress_callback_t progress_cb; // Progress callback
    void* progress_userdata; // Progress callback user data
    bool initialized; // Initialization flag
} netchunk_context_t;

/**
 * @brief File operation statistics
 */
typedef struct netchunk_stats {
    uint64_t bytes_processed; // Total bytes processed
    uint32_t chunks_processed; // Total chunks processed
    uint32_t servers_used; // Number of servers used
    double elapsed_seconds; // Operation duration
    uint32_t retries_performed; // Number of retries
} netchunk_stats_t;

/**
 * @brief Initialize NetChunk context
 *
 * @param context Pointer to context structure to initialize
 * @param config_path Path to configuration file (NULL for default)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_init(netchunk_context_t* context, const char* config_path);

/**
 * @brief Set progress callback for operations
 *
 * @param context NetChunk context
 * @param callback Progress callback function
 * @param userdata User data passed to callback
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_set_progress_callback(
    netchunk_context_t* context,
    netchunk_progress_callback_t callback,
    void* userdata);

/**
 * @brief Upload a file to the distributed storage system
 *
 * @param context NetChunk context
 * @param local_path Path to local file to upload
 * @param remote_name Remote file name identifier
 * @param stats Optional statistics output (can be NULL)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_upload(
    netchunk_context_t* context,
    const char* local_path,
    const char* remote_name,
    netchunk_stats_t* stats);

/**
 * @brief Download a file from the distributed storage system
 *
 * @param context NetChunk context
 * @param remote_name Remote file name identifier
 * @param local_path Path where to save downloaded file
 * @param stats Optional statistics output (can be NULL)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_download(
    netchunk_context_t* context,
    const char* remote_name,
    const char* local_path,
    netchunk_stats_t* stats);

/**
 * @brief List all files in the distributed storage system
 *
 * @param context NetChunk context
 * @param files Pointer to array of file manifests (allocated by function)
 * @param count Pointer to receive number of files found
 * @return NETCHUNK_SUCCESS on success, error code on failure
 *
 * @note Caller must free the returned array with netchunk_free_file_list()
 */
netchunk_error_t netchunk_list_files(
    netchunk_context_t* context,
    netchunk_file_manifest_t** files,
    size_t* count);

/**
 * @brief Free file list returned by netchunk_list_files()
 *
 * @param files File list to free
 * @param count Number of files in list
 */
void netchunk_free_file_list(netchunk_file_manifest_t* files, size_t count);

/**
 * @brief Delete a file from the distributed storage system
 *
 * @param context NetChunk context
 * @param remote_name Remote file name identifier
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_delete(
    netchunk_context_t* context,
    const char* remote_name);

/**
 * @brief Verify integrity of a file in the distributed storage system
 *
 * @param context NetChunk context
 * @param remote_name Remote file name identifier
 * @param repair If true, attempt to repair any issues found
 * @param chunks_verified Pointer to receive number of chunks verified
 * @param chunks_repaired Pointer to receive number of chunks repaired
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_verify(
    netchunk_context_t* context,
    const char* remote_name,
    bool repair,
    uint32_t* chunks_verified,
    uint32_t* chunks_repaired);

/**
 * @brief Check health of all configured servers
 *
 * @param context NetChunk context
 * @param healthy_servers Pointer to receive number of healthy servers
 * @param total_servers Pointer to receive total number of servers
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_health_check(
    netchunk_context_t* context,
    uint32_t* healthy_servers,
    uint32_t* total_servers);

/**
 * @brief Get version information
 *
 * @param major Pointer to receive major version
 * @param minor Pointer to receive minor version
 * @param patch Pointer to receive patch version
 * @param version_string Pointer to receive version string
 */
void netchunk_get_version(
    int* major,
    int* minor,
    int* patch,
    const char** version_string);

/**
 * @brief Cleanup NetChunk context and free resources
 *
 * @param context NetChunk context to cleanup
 */
void netchunk_cleanup(netchunk_context_t* context);

#ifdef __cplusplus
}
#endif

#endif /* NETCHUNK_H */
