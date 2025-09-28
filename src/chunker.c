#include "chunker.h"
#include "crypto.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Internal helper functions
static netchunk_error_t read_file_chunk(FILE* file, uint8_t* buffer, size_t buffer_size, size_t* bytes_read);
static int chunk_compare_by_sequence(const void* a, const void* b);

// Chunking Functions

netchunk_error_t netchunk_chunker_init(netchunk_chunker_context_t* context,
    const char* input_file_path,
    size_t chunk_size)
{
    if (!context || !input_file_path || chunk_size == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(context, 0, sizeof(netchunk_chunker_context_t));

    // Open input file
    context->input_file = fopen(input_file_path, "rb");
    if (!context->input_file) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    }

    // Get file size
    struct stat file_stat;
    if (stat(input_file_path, &file_stat) != 0) {
        fclose(context->input_file);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    context->chunk_size = chunk_size;
    context->total_file_size = file_stat.st_size;
    context->total_chunks = netchunk_calculate_chunk_count(file_stat.st_size, chunk_size);
    context->current_chunk_number = 0;
    context->bytes_processed = 0;
    context->finished = false;
    context->start_time = time(NULL);

    // Initialize file info
    const char* filename = strrchr(input_file_path, '/');
    filename = filename ? filename + 1 : input_file_path;
    strncpy(context->file_info.filename, filename, NETCHUNK_MAX_PATH_LEN - 1);
    context->file_info.total_size = context->total_file_size;
    context->file_info.created_timestamp = time(NULL);
    context->file_info.last_accessed = time(NULL);
    context->file_info.chunk_count = netchunk_calculate_chunk_count(context->total_file_size, chunk_size);
    context->file_info.chunk_size = chunk_size;

    // Calculate file hash
    rewind(context->input_file);
    netchunk_error_t hash_error = netchunk_sha256_hash_file(input_file_path, context->file_info.file_hash);
    if (hash_error != NETCHUNK_SUCCESS) {
        fclose(context->input_file);
        return hash_error;
    }

    // Allocate chunk buffer
    context->chunk_buffer_capacity = chunk_size;
    context->chunk_buffer = malloc(context->chunk_buffer_capacity);
    if (!context->chunk_buffer) {
        fclose(context->input_file);
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    // Reset file position for chunking
    rewind(context->input_file);

    return NETCHUNK_SUCCESS;
}

void netchunk_chunker_cleanup(netchunk_chunker_context_t* context)
{
    if (!context) {
        return;
    }

    if (context->input_file) {
        fclose(context->input_file);
        context->input_file = NULL;
    }

    if (context->chunk_buffer) {
        free(context->chunk_buffer);
        context->chunk_buffer = NULL;
    }

    context->chunk_buffer_capacity = 0;
    context->chunk_buffer_size = 0;
}

netchunk_error_t netchunk_chunker_next_chunk(netchunk_chunker_context_t* context,
    netchunk_chunk_t* chunk)
{
    if (!context || !chunk) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    if (context->finished || !context->input_file) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND; // No more chunks
    }

    // Read next chunk
    size_t bytes_read;
    netchunk_error_t read_error = read_file_chunk(context->input_file,
        context->chunk_buffer,
        context->chunk_size,
        &bytes_read);

    if (read_error != NETCHUNK_SUCCESS) {
        return read_error;
    }

    if (bytes_read == 0) {
        context->finished = true;
        return NETCHUNK_ERROR_FILE_NOT_FOUND; // No more chunks
    }

    // Initialize chunk
    netchunk_error_t chunk_error = netchunk_chunk_init(chunk, context->current_chunk_number, bytes_read);
    if (chunk_error != NETCHUNK_SUCCESS) {
        return chunk_error;
    }

    // Set chunk data
    chunk_error = netchunk_chunk_set_data(chunk, context->chunk_buffer, bytes_read);
    if (chunk_error != NETCHUNK_SUCCESS) {
        netchunk_chunk_cleanup(chunk);
        return chunk_error;
    }

    // Generate chunk ID
    char chunk_id[NETCHUNK_CHUNK_ID_LENGTH + 1];
    netchunk_error_t id_error = netchunk_generate_chunk_id(chunk_id,
        context->current_chunk_number,
        context->file_info.file_hash);
    if (id_error != NETCHUNK_SUCCESS) {
        netchunk_chunk_cleanup(chunk);
        return id_error;
    }
    strcpy(chunk->id, chunk_id);

    // Update progress
    context->current_chunk_number++;
    context->bytes_processed += bytes_read;

    // Check if this was the last chunk
    if (context->bytes_processed >= context->total_file_size) {
        context->finished = true;
    }

    return NETCHUNK_SUCCESS;
}

bool netchunk_chunker_has_next(const netchunk_chunker_context_t* context)
{
    if (!context) {
        return false;
    }

    return !context->finished && context->bytes_processed < context->total_file_size;
}

void netchunk_chunker_get_progress(const netchunk_chunker_context_t* context,
    uint32_t* chunks_processed,
    uint32_t* total_chunks,
    size_t* bytes_processed,
    size_t* total_bytes)
{
    if (!context) {
        return;
    }

    if (chunks_processed) {
        *chunks_processed = context->current_chunk_number;
    }

    if (total_chunks) {
        *total_chunks = context->file_info.chunk_count;
    }

    if (bytes_processed) {
        *bytes_processed = context->bytes_processed;
    }

    if (total_bytes) {
        *total_bytes = context->total_file_size;
    }
}

// Chunk Management Functions

netchunk_error_t netchunk_chunk_init(netchunk_chunk_t* chunk,
    uint32_t sequence_number,
    size_t data_size)
{
    if (!chunk) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(chunk, 0, sizeof(netchunk_chunk_t));

    chunk->sequence_number = sequence_number;
    chunk->size = data_size;
    chunk->created_timestamp = time(NULL);
    chunk->location_count = 0;
    chunk->data = NULL;
    chunk->data_owned = false;

    return NETCHUNK_SUCCESS;
}

void netchunk_chunk_cleanup(netchunk_chunk_t* chunk)
{
    if (!chunk) {
        return;
    }

    if (chunk->data && chunk->data_owned) {
        free(chunk->data);
        chunk->data = NULL;
    }

    chunk->data_owned = false;
    chunk->size = 0;
}

netchunk_error_t netchunk_chunk_set_data(netchunk_chunk_t* chunk,
    const uint8_t* data,
    size_t data_size)
{
    if (!chunk || !data || data_size == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Free existing data if owned
    if (chunk->data && chunk->data_owned) {
        free(chunk->data);
    }

    // Allocate and copy new data
    chunk->data = malloc(data_size);
    if (!chunk->data) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    memcpy(chunk->data, data, data_size);
    chunk->size = data_size;
    chunk->data_owned = true;

    // Calculate hash
    netchunk_error_t hash_error = netchunk_sha256_hash(data, data_size, chunk->hash);
    if (hash_error != NETCHUNK_SUCCESS) {
        free(chunk->data);
        chunk->data = NULL;
        chunk->data_owned = false;
        return hash_error;
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_chunk_verify_integrity(const netchunk_chunk_t* chunk)
{
    if (!chunk || !chunk->data) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    uint8_t computed_hash[NETCHUNK_HASH_LENGTH];
    netchunk_error_t hash_error = netchunk_sha256_hash(chunk->data, chunk->size, computed_hash);
    if (hash_error != NETCHUNK_SUCCESS) {
        return hash_error;
    }

    if (!netchunk_hash_compare(chunk->hash, computed_hash, NETCHUNK_HASH_LENGTH)) {
        return NETCHUNK_ERROR_CHUNK_INTEGRITY;
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_chunk_add_location(netchunk_chunk_t* chunk,
    int server_id,
    const char* remote_path)
{
    if (!chunk || !remote_path || server_id < 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Convert server_id to string for comparison
    char server_id_str[NETCHUNK_MAX_SERVER_ID_LEN];
    snprintf(server_id_str, sizeof(server_id_str), "%d", server_id);

    // Check if we already have this location
    for (int i = 0; i < chunk->location_count; i++) {
        if (strcmp(chunk->locations[i].server_id, server_id_str) == 0) {
            // Update existing location
            strncpy(chunk->locations[i].remote_path, remote_path, NETCHUNK_MAX_PATH_LEN - 1);
            chunk->locations[i].upload_time = time(NULL);
            chunk->locations[i].verified = false;
            return NETCHUNK_SUCCESS;
        }
    }

    // Check if we have room for another location
    if (chunk->location_count >= NETCHUNK_MAX_CHUNK_LOCATIONS) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Add new location
    netchunk_chunk_location_t* location = &chunk->locations[chunk->location_count];
    snprintf(location->server_id, sizeof(location->server_id), "%d", server_id);
    strncpy(location->remote_path, remote_path, NETCHUNK_MAX_PATH_LEN - 1);
    location->upload_time = time(NULL);
    location->verified = false;
    location->last_verified = 0;

    chunk->location_count++;

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_chunk_remove_location(netchunk_chunk_t* chunk,
    int server_id)
{
    if (!chunk || server_id < 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Convert server_id to string for comparison
    char server_id_str[NETCHUNK_MAX_SERVER_ID_LEN];
    snprintf(server_id_str, sizeof(server_id_str), "%d", server_id);

    // Find the location to remove
    for (int i = 0; i < chunk->location_count; i++) {
        if (strcmp(chunk->locations[i].server_id, server_id_str) == 0) {
            // Move remaining locations down
            for (int j = i; j < chunk->location_count - 1; j++) {
                chunk->locations[j] = chunk->locations[j + 1];
            }
            chunk->location_count--;
            return NETCHUNK_SUCCESS;
        }
    }

    return NETCHUNK_ERROR_FILE_NOT_FOUND; // Location not found
}

netchunk_chunk_location_t* netchunk_chunk_find_location(const netchunk_chunk_t* chunk,
    int server_id)
{
    if (!chunk || server_id < 0) {
        return NULL;
    }

    // Convert server_id to string for comparison
    char server_id_str[NETCHUNK_MAX_SERVER_ID_LEN];
    snprintf(server_id_str, sizeof(server_id_str), "%d", server_id);

    for (int i = 0; i < chunk->location_count; i++) {
        if (strcmp(chunk->locations[i].server_id, server_id_str) == 0) {
            return (netchunk_chunk_location_t*)&chunk->locations[i];
        }
    }

    return NULL;
}

netchunk_chunk_location_t* netchunk_chunk_get_best_location(const netchunk_chunk_t* chunk,
    const int* server_preferences,
    int preference_count)
{
    if (!chunk || chunk->location_count == 0) {
        return NULL;
    }

    // If preferences provided, try those first
    if (server_preferences && preference_count > 0) {
        for (int i = 0; i < preference_count; i++) {
            netchunk_chunk_location_t* location = netchunk_chunk_find_location(chunk, server_preferences[i]);
            if (location) {
                return location;
            }
        }
    }

    // Return first available location
    return (netchunk_chunk_location_t*)&chunk->locations[0];
}

// File Reconstruction Functions

netchunk_error_t netchunk_reconstruct_file_init(const char* output_file_path,
    const netchunk_file_info_t* file_info,
    netchunk_chunk_t* chunks,
    uint32_t chunk_count)
{
    if (!output_file_path || !file_info || !chunks || chunk_count == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Verify we have all chunks
    if (chunk_count != file_info->chunk_count) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Sort chunks by sequence number
    netchunk_sort_chunks_by_sequence(chunks, chunk_count);

    // Verify chunk sequence is complete
    for (uint32_t i = 0; i < chunk_count; i++) {
        if (chunks[i].sequence_number != i) {
            return NETCHUNK_ERROR_INVALID_ARGUMENT;
        }
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_reconstruct_file(const char* output_file_path,
    netchunk_chunk_t* chunks,
    uint32_t chunk_count,
    netchunk_chunk_progress_callback_t progress_callback,
    void* progress_userdata)
{
    if (!output_file_path || !chunks || chunk_count == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    FILE* output_file = fopen(output_file_path, "wb");
    if (!output_file) {
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    size_t total_bytes_written = 0;
    size_t expected_total_size = 0;

    // Calculate expected total size
    for (uint32_t i = 0; i < chunk_count; i++) {
        expected_total_size += chunks[i].size;
    }

    // Write chunks in sequence
    for (uint32_t i = 0; i < chunk_count; i++) {
        netchunk_chunk_t* chunk = &chunks[i];

        if (!chunk->data) {
            fclose(output_file);
            unlink(output_file_path); // Remove partial file
            return NETCHUNK_ERROR_INVALID_ARGUMENT;
        }

        // Verify chunk integrity
        netchunk_error_t verify_error = netchunk_chunk_verify_integrity(chunk);
        if (verify_error != NETCHUNK_SUCCESS) {
            fclose(output_file);
            unlink(output_file_path);
            return verify_error;
        }

        // Write chunk data
        size_t bytes_written = fwrite(chunk->data, 1, chunk->size, output_file);
        if (bytes_written != chunk->size) {
            fclose(output_file);
            unlink(output_file_path);
            return NETCHUNK_ERROR_FILE_ACCESS;
        }

        total_bytes_written += bytes_written;

        // Call progress callback if provided
        if (progress_callback) {
            progress_callback(progress_userdata, i + 1, chunk_count,
                total_bytes_written, expected_total_size);
        }
    }

    fclose(output_file);

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_verify_reconstructed_file(const char* file_path,
    const netchunk_file_info_t* expected_file_info)
{
    if (!file_path || !expected_file_info) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Check file size
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    }

    if ((size_t)file_stat.st_size != expected_file_info->total_size) {
        return NETCHUNK_ERROR_CHUNK_INTEGRITY;
    }

    // Calculate and verify file hash
    uint8_t computed_hash[NETCHUNK_HASH_LENGTH];
    netchunk_error_t hash_error = netchunk_sha256_hash_file(file_path, computed_hash);
    if (hash_error != NETCHUNK_SUCCESS) {
        return hash_error;
    }

    if (!netchunk_hash_compare(expected_file_info->file_hash, computed_hash, NETCHUNK_HASH_LENGTH)) {
        return NETCHUNK_ERROR_CHUNK_INTEGRITY;
    }

    return NETCHUNK_SUCCESS;
}

// Utility Functions

netchunk_error_t netchunk_generate_chunk_id(char* chunk_id,
    uint32_t sequence_number,
    const uint8_t* file_hash)
{
    if (!chunk_id || !file_hash) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Generate random component for uniqueness
    uint8_t random_bytes[8];
    netchunk_error_t random_error = netchunk_generate_random_bytes(random_bytes, sizeof(random_bytes));
    if (random_error != NETCHUNK_SUCCESS) {
        return random_error;
    }

    // Create chunk ID from sequence number, file hash prefix, and random bytes
    snprintf(chunk_id, NETCHUNK_CHUNK_ID_LENGTH + 1,
        "%08x%02x%02x%02x%02x%02x%02x",
        sequence_number,
        file_hash[0], file_hash[1],
        random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3]);

    return NETCHUNK_SUCCESS;
}

uint32_t netchunk_calculate_chunk_count(size_t file_size, size_t target_chunk_size)
{
    if (file_size == 0 || target_chunk_size == 0) {
        return 0;
    }

    return (uint32_t)((file_size + target_chunk_size - 1) / target_chunk_size);
}

void netchunk_sort_chunks_by_sequence(netchunk_chunk_t* chunks, uint32_t chunk_count)
{
    if (!chunks || chunk_count <= 1) {
        return;
    }

    qsort(chunks, chunk_count, sizeof(netchunk_chunk_t), chunk_compare_by_sequence);
}

netchunk_chunk_t* netchunk_find_chunk_by_id(netchunk_chunk_t* chunks,
    uint32_t chunk_count,
    const char* chunk_id)
{
    if (!chunks || !chunk_id || chunk_count == 0) {
        return NULL;
    }

    for (uint32_t i = 0; i < chunk_count; i++) {
        if (strcmp(chunks[i].id, chunk_id) == 0) {
            return &chunks[i];
        }
    }

    return NULL;
}

bool netchunk_verify_chunk_replicas(const netchunk_chunk_t* chunks,
    uint32_t chunk_count,
    int min_replicas)
{
    if (!chunks || chunk_count == 0 || min_replicas <= 0) {
        return false;
    }

    for (uint32_t i = 0; i < chunk_count; i++) {
        if (chunks[i].location_count < min_replicas) {
            return false;
        }
    }

    return true;
}

void netchunk_get_chunk_statistics(const netchunk_chunk_t* chunks,
    uint32_t chunk_count,
    size_t* total_size,
    double* avg_replicas,
    int* min_replicas,
    int* max_replicas)
{
    if (!chunks || chunk_count == 0) {
        return;
    }

    size_t size_sum = 0;
    int replica_sum = 0;
    int min_reps = chunks[0].location_count;
    int max_reps = chunks[0].location_count;

    for (uint32_t i = 0; i < chunk_count; i++) {
        size_sum += chunks[i].size;
        replica_sum += chunks[i].location_count;

        if (chunks[i].location_count < min_reps) {
            min_reps = chunks[i].location_count;
        }
        if (chunks[i].location_count > max_reps) {
            max_reps = chunks[i].location_count;
        }
    }

    if (total_size) {
        *total_size = size_sum;
    }

    if (avg_replicas) {
        *avg_replicas = (double)replica_sum / chunk_count;
    }

    if (min_replicas) {
        *min_replicas = min_reps;
    }

    if (max_replicas) {
        *max_replicas = max_reps;
    }
}

// Internal helper functions

static netchunk_error_t read_file_chunk(FILE* file, uint8_t* buffer, size_t buffer_size, size_t* bytes_read)
{
    if (!file || !buffer || !bytes_read) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    *bytes_read = fread(buffer, 1, buffer_size, file);

    if (*bytes_read == 0) {
        if (feof(file)) {
            return NETCHUNK_SUCCESS; // End of file reached
        } else if (ferror(file)) {
            return NETCHUNK_ERROR_FILE_ACCESS;
        }
    }

    return NETCHUNK_SUCCESS;
}

static int chunk_compare_by_sequence(const void* a, const void* b)
{
    const netchunk_chunk_t* chunk_a = (const netchunk_chunk_t*)a;
    const netchunk_chunk_t* chunk_b = (const netchunk_chunk_t*)b;

    if (chunk_a->sequence_number < chunk_b->sequence_number) {
        return -1;
    } else if (chunk_a->sequence_number > chunk_b->sequence_number) {
        return 1;
    } else {
        return 0;
    }
}
