#ifndef NETCHUNK_CHUNKER_H
#define NETCHUNK_CHUNKER_H

#include "config.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Chunk processing constants
#define NETCHUNK_CHUNK_ID_LENGTH 16 // UUID-like chunk ID
#define NETCHUNK_HASH_LENGTH 32 // SHA-256 hash length
#define NETCHUNK_MAX_CHUNK_LOCATIONS NETCHUNK_MAX_REPLICATION_FACTOR
#define NETCHUNK_READ_BUFFER_SIZE (64 * 1024) // 64KB read buffer

// Forward declarations
typedef struct netchunk_chunk netchunk_chunk_t;
typedef struct netchunk_chunker_context netchunk_chunker_context_t;
typedef struct netchunk_file_info netchunk_file_info_t;

// Chunk location on server (define before using in netchunk_chunk)
typedef struct netchunk_chunk_location {
    char server_id[NETCHUNK_MAX_SERVER_ID_LEN]; // Server ID string
    char remote_path[NETCHUNK_MAX_PATH_LEN]; // Full path on server
    time_t upload_time; // When uploaded to this server
    bool verified; // Whether integrity was verified
    time_t last_verified; // Last verification timestamp
} netchunk_chunk_location_t;

// Chunk metadata structure
typedef struct netchunk_chunk {
    char id[NETCHUNK_CHUNK_ID_LENGTH + 1]; // Null-terminated chunk ID
    uint8_t hash[NETCHUNK_HASH_LENGTH]; // SHA-256 hash of chunk data
    size_t size; // Actual size of chunk data
    uint32_t sequence_number; // Order in original file
    time_t created_timestamp; // When chunk was created

    // Server location tracking
    netchunk_chunk_location_t locations[NETCHUNK_MAX_CHUNK_LOCATIONS];
    int location_count; // Number of servers storing this chunk

    // Data pointer (for in-memory operations)
    uint8_t* data; // Chunk data (NULL if not loaded)
    bool data_owned; // Whether this structure owns the data
} netchunk_chunk_t;

// File information structure
typedef struct netchunk_file_info {
    char filename[NETCHUNK_MAX_PATH_LEN]; // Original filename
    size_t total_size; // Total file size
    time_t created_timestamp; // When file was chunked
    time_t last_accessed; // Last access time
    uint32_t chunk_count; // Number of chunks
    uint8_t file_hash[NETCHUNK_HASH_LENGTH]; // Hash of entire original file
    size_t chunk_size; // Size used for chunking
} netchunk_file_info_t;

// Chunker context for streaming operations
typedef struct netchunk_chunker_context {
    FILE* input_file; // Input file handle
    size_t chunk_size; // Target chunk size
    uint32_t current_chunk_number; // Current chunk being processed
    uint32_t total_chunks; // Total number of chunks
    size_t total_file_size; // Total size of input file
    size_t bytes_processed; // Bytes processed so far

    // Current chunk being built
    uint8_t* chunk_buffer; // Buffer for current chunk
    size_t chunk_buffer_size; // Current buffer size
    size_t chunk_buffer_capacity; // Buffer capacity

    // File information
    netchunk_file_info_t file_info; // File metadata

    // Progress tracking
    time_t start_time; // When chunking started
    bool finished; // Whether all chunks processed
} netchunk_chunker_context_t;

// Progress callback for chunking operations
typedef void (*netchunk_chunk_progress_callback_t)(void* userdata,
    uint32_t chunks_processed,
    uint32_t total_chunks,
    size_t bytes_processed,
    size_t total_bytes);

// Chunking Functions

/**
 * @brief Initialize chunker context for file processing
 * @param context Chunker context to initialize
 * @param input_file_path Path to input file
 * @param chunk_size Size for each chunk
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_chunker_init(netchunk_chunker_context_t* context,
    const char* input_file_path,
    size_t chunk_size);

/**
 * @brief Cleanup chunker context and free resources
 * @param context Chunker context to cleanup
 */
void netchunk_chunker_cleanup(netchunk_chunker_context_t* context);

/**
 * @brief Get the next chunk from the file
 * @param context Chunker context
 * @param chunk Output chunk structure
 * @return NETCHUNK_SUCCESS if chunk retrieved, NETCHUNK_ERROR_FILE_NOT_FOUND if no more chunks
 */
netchunk_error_t netchunk_chunker_next_chunk(netchunk_chunker_context_t* context,
    netchunk_chunk_t* chunk);

/**
 * @brief Check if more chunks are available
 * @param context Chunker context
 * @return true if more chunks available, false otherwise
 */
bool netchunk_chunker_has_next(const netchunk_chunker_context_t* context);

/**
 * @brief Get progress information
 * @param context Chunker context
 * @param chunks_processed Output number of chunks processed
 * @param total_chunks Output total number of chunks
 * @param bytes_processed Output bytes processed
 * @param total_bytes Output total bytes
 */
void netchunk_chunker_get_progress(const netchunk_chunker_context_t* context,
    uint32_t* chunks_processed,
    uint32_t* total_chunks,
    size_t* bytes_processed,
    size_t* total_bytes);

// Chunk Management Functions

/**
 * @brief Initialize a chunk structure
 * @param chunk Chunk to initialize
 * @param sequence_number Sequence number in file
 * @param data_size Size of chunk data
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_chunk_init(netchunk_chunk_t* chunk,
    uint32_t sequence_number,
    size_t data_size);

/**
 * @brief Cleanup chunk structure and free data
 * @param chunk Chunk to cleanup
 */
void netchunk_chunk_cleanup(netchunk_chunk_t* chunk);

/**
 * @brief Set chunk data and calculate hash
 * @param chunk Target chunk
 * @param data Data to copy into chunk
 * @param data_size Size of data
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_chunk_set_data(netchunk_chunk_t* chunk,
    const uint8_t* data,
    size_t data_size);

/**
 * @brief Verify chunk data against stored hash
 * @param chunk Chunk to verify
 * @return NETCHUNK_SUCCESS if hash matches, error code if corrupted
 */
netchunk_error_t netchunk_chunk_verify_integrity(const netchunk_chunk_t* chunk);

/**
 * @brief Add server location to chunk
 * @param chunk Target chunk
 * @param server_id Server index
 * @param remote_path Path on server
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_chunk_add_location(netchunk_chunk_t* chunk,
    int server_id,
    const char* remote_path);

/**
 * @brief Remove server location from chunk
 * @param chunk Target chunk
 * @param server_id Server index to remove
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_chunk_remove_location(netchunk_chunk_t* chunk,
    int server_id);

/**
 * @brief Find chunk location by server ID
 * @param chunk Target chunk
 * @param server_id Server ID to find
 * @return Pointer to location if found, NULL otherwise
 */
netchunk_chunk_location_t* netchunk_chunk_find_location(const netchunk_chunk_t* chunk,
    int server_id);

/**
 * @brief Get best server location for chunk retrieval
 * @param chunk Target chunk
 * @param server_preferences Array of preferred server IDs (optional)
 * @param preference_count Number of preferences
 * @return Pointer to best location, NULL if no locations available
 */
netchunk_chunk_location_t* netchunk_chunk_get_best_location(const netchunk_chunk_t* chunk,
    const int* server_preferences,
    int preference_count);

// File Reconstruction Functions

/**
 * @brief Initialize file reconstruction from chunks
 * @param output_file_path Output file path
 * @param file_info File information structure
 * @param chunks Array of chunks to reconstruct from
 * @param chunk_count Number of chunks
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_reconstruct_file_init(const char* output_file_path,
    const netchunk_file_info_t* file_info,
    netchunk_chunk_t* chunks,
    uint32_t chunk_count);

/**
 * @brief Reconstruct file from chunk data
 * @param output_file_path Output file path
 * @param chunks Array of chunks (must be sorted by sequence_number)
 * @param chunk_count Number of chunks
 * @param progress_callback Progress callback function (optional)
 * @param progress_userdata User data for progress callback
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_reconstruct_file(const char* output_file_path,
    netchunk_chunk_t* chunks,
    uint32_t chunk_count,
    netchunk_chunk_progress_callback_t progress_callback,
    void* progress_userdata);

/**
 * @brief Verify reconstructed file matches original
 * @param file_path File to verify
 * @param expected_file_info Expected file information
 * @return NETCHUNK_SUCCESS if file matches, error code if corrupted
 */
netchunk_error_t netchunk_verify_reconstructed_file(const char* file_path,
    const netchunk_file_info_t* expected_file_info);

// Utility Functions

/**
 * @brief Generate unique chunk ID
 * @param chunk_id Output buffer for chunk ID (must be at least NETCHUNK_CHUNK_ID_LENGTH + 1)
 * @param sequence_number Sequence number to incorporate
 * @param file_hash Hash of source file
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_generate_chunk_id(char* chunk_id,
    uint32_t sequence_number,
    const uint8_t* file_hash);

/**
 * @brief Calculate optimal number of chunks for file size
 * @param file_size Size of file
 * @param target_chunk_size Target chunk size
 * @return Number of chunks needed
 */
uint32_t netchunk_calculate_chunk_count(size_t file_size, size_t target_chunk_size);

/**
 * @brief Sort chunks by sequence number
 * @param chunks Array of chunks to sort
 * @param chunk_count Number of chunks
 */
void netchunk_sort_chunks_by_sequence(netchunk_chunk_t* chunks, uint32_t chunk_count);

/**
 * @brief Find chunk by ID in array
 * @param chunks Array of chunks to search
 * @param chunk_count Number of chunks
 * @param chunk_id Chunk ID to find
 * @return Pointer to chunk if found, NULL otherwise
 */
netchunk_chunk_t* netchunk_find_chunk_by_id(netchunk_chunk_t* chunks,
    uint32_t chunk_count,
    const char* chunk_id);

/**
 * @brief Check if all chunks have minimum required replicas
 * @param chunks Array of chunks
 * @param chunk_count Number of chunks
 * @param min_replicas Minimum required replicas
 * @return true if all chunks have enough replicas, false otherwise
 */
bool netchunk_verify_chunk_replicas(const netchunk_chunk_t* chunks,
    uint32_t chunk_count,
    int min_replicas);

/**
 * @brief Get chunk statistics
 * @param chunks Array of chunks
 * @param chunk_count Number of chunks
 * @param total_size Output total size of all chunks
 * @param avg_replicas Output average number of replicas per chunk
 * @param min_replicas Output minimum replicas found
 * @param max_replicas Output maximum replicas found
 */
void netchunk_get_chunk_statistics(const netchunk_chunk_t* chunks,
    uint32_t chunk_count,
    size_t* total_size,
    double* avg_replicas,
    int* min_replicas,
    int* max_replicas);

#ifdef __cplusplus
}
#endif

#endif // NETCHUNK_CHUNKER_H
