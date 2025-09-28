#ifndef NETCHUNK_MANIFEST_H
#define NETCHUNK_MANIFEST_H

#include "chunker.h"
#include "config.h"
#include <cjson/cJSON.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct netchunk_file_manifest netchunk_file_manifest_t;
typedef struct netchunk_manifest_manager netchunk_manifest_manager_t;

// Manifest constants
#define NETCHUNK_MANIFEST_VERSION "1.0"
#define NETCHUNK_MANIFEST_EXTENSION ".netchunk"
#define NETCHUNK_MANIFEST_TEMP_SUFFIX ".tmp"
#define NETCHUNK_MAX_MANIFEST_PATH_LEN 1024

// File manifest structure (matches netchunk_file_info_t but with additional manifest-specific fields)
typedef struct netchunk_file_manifest {
    // File identification
    char original_filename[NETCHUNK_MAX_PATH_LEN];
    char manifest_id[64]; // Unique manifest identifier
    char version[16]; // Manifest format version

    // File metadata
    size_t total_size; // Total file size
    size_t original_size; // Original file size (alias for total_size)
    uint8_t file_hash[NETCHUNK_HASH_LENGTH]; // SHA-256 hash of entire file
    size_t chunk_size; // Chunk size used
    uint32_t chunk_count; // Number of chunks

    // Timestamps
    time_t created_timestamp; // When file was chunked
    time_t last_accessed; // Last access time
    time_t last_modified; // Last modification time
    time_t last_verified; // Last integrity check

    // Chunks array
    netchunk_chunk_t* chunks; // Array of chunks
    bool chunks_owned; // Whether this structure owns the chunks array

    // Replication settings
    int replication_factor; // Number of replicas per chunk
    int min_replicas_required; // Minimum replicas needed for reconstruction

    // Manifest metadata
    char creator_info[256]; // Information about who created the manifest
    char comment[512]; // Optional comment
} netchunk_file_manifest_t;

// Manifest manager for handling multiple manifests
typedef struct netchunk_manifest_manager {
    char manifest_directory[NETCHUNK_MAX_PATH_LEN]; // Directory containing manifest files
    netchunk_config_t* config; // Configuration reference
    bool auto_backup; // Whether to backup manifests
    int max_backups; // Maximum number of backups to keep
} netchunk_manifest_manager_t;

// Manifest Management Functions

/**
 * @brief Initialize manifest manager
 * @param manager Manifest manager to initialize
 * @param manifest_directory Directory to store manifest files
 * @param config NetChunk configuration
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_manager_init(netchunk_manifest_manager_t* manager,
    const char* manifest_directory,
    netchunk_config_t* config);

/**
 * @brief Cleanup manifest manager
 * @param manager Manifest manager to cleanup
 */
void netchunk_manifest_manager_cleanup(netchunk_manifest_manager_t* manager);

/**
 * @brief Create manifest directory if it doesn't exist
 * @param manager Manifest manager
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_ensure_directory(netchunk_manifest_manager_t* manager);

// File Manifest Functions

/**
 * @brief Initialize a file manifest structure
 * @param manifest Manifest to initialize
 * @param file_info File information to populate from
 * @param chunks Array of chunks (optional, can be NULL)
 * @param chunk_count Number of chunks
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_file_manifest_init(netchunk_file_manifest_t* manifest,
    const netchunk_file_info_t* file_info,
    netchunk_chunk_t* chunks,
    uint32_t chunk_count);

/**
 * @brief Initialize a file manifest structure (simple version)
 * @param manifest Manifest to initialize
 * @param remote_name Remote file name
 * @param file_size File size
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_init(netchunk_file_manifest_t* manifest,
    const char* remote_name,
    size_t file_size);

/**
 * @brief Add chunk to manifest
 * @param manifest Manifest to add chunk to
 * @param chunk Chunk to add
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_add_chunk(netchunk_file_manifest_t* manifest,
    const netchunk_chunk_t* chunk);

/**
 * @brief Cleanup manifest structure (alias for netchunk_file_manifest_cleanup)
 * @param manifest Manifest to cleanup
 */
void netchunk_manifest_cleanup(netchunk_file_manifest_t* manifest);

/**
 * @brief Cleanup file manifest and free resources
 * @param manifest Manifest to cleanup
 */
void netchunk_file_manifest_cleanup(netchunk_file_manifest_t* manifest);

/**
 * @brief Create file manifest from chunker context
 * @param manifest Output manifest structure
 * @param chunker_context Chunker context with file info
 * @param chunks Array of chunks
 * @param chunk_count Number of chunks
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_file_manifest_create_from_chunker(netchunk_file_manifest_t* manifest,
    const netchunk_chunker_context_t* chunker_context,
    netchunk_chunk_t* chunks,
    uint32_t chunk_count);

/**
 * @brief Generate unique manifest ID
 * @param manifest_id Output buffer for manifest ID (at least 64 bytes)
 * @param filename Original filename
 * @param file_hash File hash
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_generate_manifest_id(char* manifest_id,
    const char* filename,
    const uint8_t* file_hash);

// JSON Serialization Functions

/**
 * @brief Serialize file manifest to JSON
 * @param manifest File manifest to serialize
 * @param json_output Output JSON string (caller must free)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_file_manifest_to_json(const netchunk_file_manifest_t* manifest,
    char** json_output);

/**
 * @brief Deserialize file manifest from JSON
 * @param json_input JSON string input
 * @param manifest Output manifest structure
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_file_manifest_from_json(const char* json_input,
    netchunk_file_manifest_t* manifest);

/**
 * @brief Serialize chunk to JSON object
 * @param chunk Chunk to serialize
 * @return JSON object (caller must free with cJSON_Delete) or NULL on error
 */
cJSON* netchunk_chunk_to_json(const netchunk_chunk_t* chunk);

/**
 * @brief Deserialize chunk from JSON object
 * @param json JSON object containing chunk data
 * @param chunk Output chunk structure
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_chunk_from_json(const cJSON* json, netchunk_chunk_t* chunk);

// File Operations

/**
 * @brief Save manifest to file
 * @param manager Manifest manager
 * @param manifest Manifest to save
 * @param filename Manifest filename (without extension)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_save_to_file(netchunk_manifest_manager_t* manager,
    const netchunk_file_manifest_t* manifest,
    const char* filename);

/**
 * @brief Load manifest from file
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension)
 * @param manifest Output manifest structure
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_load_from_file(netchunk_manifest_manager_t* manager,
    const char* filename,
    netchunk_file_manifest_t* manifest);

/**
 * @brief Delete manifest file
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_delete_file(netchunk_manifest_manager_t* manager,
    const char* filename);

/**
 * @brief List all manifest files
 * @param manager Manifest manager
 * @param filenames Output array of filenames (caller must free)
 * @param count Output count of files
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_list_files(netchunk_manifest_manager_t* manager,
    char*** filenames,
    size_t* count);

/**
 * @brief Check if manifest file exists
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension)
 * @return true if file exists, false otherwise
 */
bool netchunk_manifest_file_exists(netchunk_manifest_manager_t* manager,
    const char* filename);

// Backup and Recovery Functions

/**
 * @brief Create backup of manifest file
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_backup(netchunk_manifest_manager_t* manager,
    const char* filename);

/**
 * @brief Restore manifest from backup
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension)
 * @param backup_timestamp Timestamp of backup to restore (0 for latest)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_restore_backup(netchunk_manifest_manager_t* manager,
    const char* filename,
    time_t backup_timestamp);

/**
 * @brief List available backups for a manifest
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension)
 * @param backup_timestamps Output array of backup timestamps (caller must free)
 * @param count Output count of backups
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_list_backups(netchunk_manifest_manager_t* manager,
    const char* filename,
    time_t** backup_timestamps,
    size_t* count);

/**
 * @brief Cleanup old backups (keep only max_backups most recent)
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension, NULL for all manifests)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_cleanup_backups(netchunk_manifest_manager_t* manager,
    const char* filename);

// Validation and Integrity Functions

/**
 * @brief Validate manifest structure and data
 * @param manifest Manifest to validate
 * @return NETCHUNK_SUCCESS if valid, error code if invalid
 */
netchunk_error_t netchunk_manifest_validate(const netchunk_file_manifest_t* manifest);

/**
 * @brief Verify manifest file integrity
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension)
 * @return NETCHUNK_SUCCESS if valid, error code if corrupted
 */
netchunk_error_t netchunk_manifest_verify_file(netchunk_manifest_manager_t* manager,
    const char* filename);

/**
 * @brief Update manifest timestamps
 * @param manifest Manifest to update
 * @param update_accessed Whether to update last_accessed
 * @param update_modified Whether to update last_modified
 * @param update_verified Whether to update last_verified
 */
void netchunk_manifest_update_timestamps(netchunk_file_manifest_t* manifest,
    bool update_accessed,
    bool update_modified,
    bool update_verified);

// Utility Functions

/**
 * @brief Build full manifest file path
 * @param manager Manifest manager
 * @param filename Manifest filename (without extension)
 * @param full_path Output buffer for full path
 * @param path_len Size of output buffer
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_build_path(netchunk_manifest_manager_t* manager,
    const char* filename,
    char* full_path,
    size_t path_len);

/**
 * @brief Get manifest statistics
 * @param manifest Manifest to analyze
 * @param total_chunks_stored Output total chunks with at least one replica
 * @param total_chunk_size Output total size of all chunks
 * @param avg_replicas Output average replicas per chunk
 * @param min_replicas Output minimum replicas found
 * @param missing_chunks Output count of chunks with no replicas
 */
void netchunk_manifest_get_statistics(const netchunk_file_manifest_t* manifest,
    uint32_t* total_chunks_stored,
    size_t* total_chunk_size,
    double* avg_replicas,
    int* min_replicas,
    uint32_t* missing_chunks);

/**
 * @brief Find chunks that need replication
 * @param manifest Manifest to analyze
 * @param min_replicas Minimum required replicas
 * @param chunk_indices Output array of chunk indices needing replication (caller must free)
 * @param count Output count of chunks needing replication
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_manifest_find_under_replicated_chunks(const netchunk_file_manifest_t* manifest,
    int min_replicas,
    uint32_t** chunk_indices,
    size_t* count);

#ifdef __cplusplus
}
#endif

#endif // NETCHUNK_MANIFEST_H
